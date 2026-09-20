// nreplatformd - NREPlatform LiteV
//
// Applies per-port latency / packet loss / speed limits (Linux netem) from
// /etc/config/nreplatform and keeps them applied when interfaces come and go.
//
//   SIGHUP  reload configuration
//   SIGTERM remove all rules and exit

#include "config.hpp"
#include "netlink.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <net/if.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nre;

namespace {

const char *kIfbPrefix = "ifbnre";

struct Applied {
    Rule        rule;
    int         ifindex = 0;     // ifindex the rule is currently applied to
    int         ifbIndex = 0;
    std::string ifbName;
    std::string state = "waiting";
};

void logf(int prio, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logf(int prio, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsyslog(prio, fmt, ap);
    va_end(ap);
}

class Engine {
public:
    Engine(std::string confdir, std::string statusFile)
        : confdir_(std::move(confdir)), statusFile_(std::move(statusFile)) {}

    bool ready() const { return nl_.ok(); }

    void reload() {
        clearAll();
        std::vector<Rule> rules;
        std::string err;
        if (!loadRules("nreplatform", confdir_, rules, err)) {
            logf(LOG_ERR, "cannot read config: %s", err.c_str());
            writeStatus();
            return;
        }
        for (size_t i = 0; i < rules.size(); ++i) {
            Applied a;
            a.rule = rules[i];
            a.ifbName = std::string(kIfbPrefix) + std::to_string(i);
            applied_.push_back(std::move(a));
        }
        for (auto &a : applied_) tryApply(a);
        writeStatus();
    }

    void clearAll() {
        for (auto &a : applied_) teardown(a);
        applied_.clear();
        writeStatus();
    }

    // An interface appeared (or was recreated) or disappeared.
    void onLink(const std::string &name, int ifindex, bool removed) {
        if (name.compare(0, std::strlen(kIfbPrefix), kIfbPrefix) == 0) return;
        bool changed = false;
        for (auto &a : applied_) {
            if (a.rule.device != name) continue;
            if (removed) {
                if (a.ifindex == 0) continue;
                teardown(a, /*deviceGone=*/true);
                a.state = "waiting: device not found";
                changed = true;
            } else if (a.ifindex != ifindex) {
                teardown(a, /*deviceGone=*/true);
                tryApply(a);
                changed = true;
            }
        }
        if (changed) writeStatus();
    }

    // After lost netlink events: re-check everything.
    void resync() {
        for (auto &a : applied_) {
            int idx = static_cast<int>(if_nametoindex(a.rule.device.c_str()));
            if (idx != a.ifindex) {
                teardown(a, /*deviceGone=*/true);
                tryApply(a);
            }
        }
        writeStatus();
    }

private:
    void tryApply(Applied &a) {
        const Rule &r = a.rule;
        if (!r.params.any()) {
            a.state = "inactive: all values are 0";
            return;
        }
        int idx = static_cast<int>(if_nametoindex(r.device.c_str()));
        if (idx == 0) {
            a.state = "waiting: device not found";
            return;
        }

        int res = 0;
        if (r.dir == Direction::Egress || r.dir == Direction::Both) {
            res = nl_.setNetem(idx, r.params);
            if (res < 0) {
                fail(a, "egress netem", res);
                return;
            }
        }
        if (r.dir == Direction::Ingress || r.dir == Direction::Both) {
            int ifb = 0;
            res = nl_.createIfb(a.ifbName, ifb);
            if (res < 0) { fail(a, "create ifb", res); rollback(a, idx); return; }
            a.ifbIndex = ifb;
            res = nl_.setNetem(ifb, r.params);
            if (res < 0) { fail(a, "ingress netem", res); rollback(a, idx); return; }
            res = nl_.setIngressRedirect(idx, ifb);
            if (res < 0) { fail(a, "ingress redirect", res); rollback(a, idx); return; }
        }

        a.ifindex = idx;
        a.state = "active";
        logf(LOG_INFO, "%s: rule applied (%s)", r.device.c_str(), directionName(r.dir));
    }

    void fail(Applied &a, const char *what, int err) {
        std::string msg = std::strerror(-err);
        if (err == -ENOENT || err == -EOPNOTSUPP)
            msg = "kernel module missing (install kmod-sched, kmod-ifb)";
        a.state = std::string("error: ") + what + ": " + msg;
        logf(LOG_WARNING, "%s: %s: %s", a.rule.device.c_str(), what, msg.c_str());
    }

    void rollback(Applied &a, int idx) {
        if (a.rule.dir != Direction::Ingress) nl_.clearRoot(idx);
        nl_.clearIngress(idx);
        removeIfb(a);
    }

    void removeIfb(Applied &a) {
        if (a.ifbIndex) {
            nl_.deleteLink(a.ifbIndex);
            a.ifbIndex = 0;
        }
    }

    void teardown(Applied &a, bool deviceGone = false) {
        if (a.ifindex && !deviceGone) {
            // Errors are expected if the device was already removed.
            nl_.clearRoot(a.ifindex);
            nl_.clearIngress(a.ifindex);
        }
        removeIfb(a);
        a.ifindex = 0;
    }

    void writeStatus() {
        std::string tmp = statusFile_ + ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "w");
        if (!f) return;
        std::fprintf(f, "nreplatformd running, %zu rule(s)\n", applied_.size());
        for (const auto &a : applied_) {
            const auto &p = a.rule.params;
            std::fprintf(f, "%-12s %-28s direction=%s delay=%gms jitter=%gms loss=%g%% rate=%gMbit/s\n",
                         a.rule.device.c_str(), a.state.c_str(), directionName(a.rule.dir),
                         p.latency_ms, p.jitter_ms, p.loss_pct, p.rate_mbit);
        }
        std::fclose(f);
        std::rename(tmp.c_str(), statusFile_.c_str());
    }

    Netlink nl_;
    std::string confdir_;
    std::string statusFile_;
    std::vector<Applied> applied_;
};

// Remove ifb devices left behind by a crashed previous run.
void removeStaleIfbs() {
    Netlink nl;
    struct if_nameindex *list = if_nameindex();
    if (!list) return;
    for (struct if_nameindex *i = list; i->if_index != 0; ++i)
        if (std::strncmp(i->if_name, kIfbPrefix, std::strlen(kIfbPrefix)) == 0)
            nl.deleteLink(static_cast<int>(i->if_index));
    if_freenameindex(list);
}

void usage(const char *argv0) {
    std::fprintf(stderr,
                 "Usage: %s [-v] [-C confdir] [-s statusfile]\n"
                 "  -v  log to stderr as well\n"
                 "  -C  UCI config directory (default /etc/config)\n"
                 "  -s  status file (default /var/run/nreplatform.status)\n",
                 argv0);
}

} // namespace

int main(int argc, char **argv) {
    std::string confdir;
    std::string statusFile = "/var/run/nreplatform.status";
    int logopt = LOG_PID;

    int c;
    while ((c = getopt(argc, argv, "vC:s:h")) != -1) {
        switch (c) {
        case 'v': logopt |= LOG_PERROR; break;
        case 'C': confdir = optarg; break;
        case 's': statusFile = optarg; break;
        default:  usage(argv[0]); return c == 'h' ? 0 : 1;
        }
    }
    openlog("nreplatformd", logopt, LOG_DAEMON);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) {
        logf(LOG_ERR, "signalfd: %s", std::strerror(errno));
        return 1;
    }

    removeStaleIfbs();

    Engine engine(confdir, statusFile);
    LinkMonitor monitor;
    if (!engine.ready() || monitor.fd() < 0) {
        logf(LOG_ERR, "cannot open netlink socket (need root / CAP_NET_ADMIN)");
        return 1;
    }

    engine.reload();
    logf(LOG_INFO, "started");

    pollfd fds[2] = {{sfd, POLLIN, 0}, {monitor.fd(), POLLIN, 0}};
    for (;;) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            logf(LOG_ERR, "poll: %s", std::strerror(errno));
            break;
        }
        if (fds[0].revents & POLLIN) {
            signalfd_siginfo si;
            if (read(sfd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGHUP) {
                    logf(LOG_INFO, "reloading configuration");
                    engine.reload();
                } else {
                    break;
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            bool lost = monitor.dispatch([&](const std::string &n, int idx, bool rm) {
                engine.onLink(n, idx, rm);
            });
            if (lost) engine.resync();
        }
    }

    engine.clearAll();
    std::remove(statusFile.c_str());
    logf(LOG_INFO, "stopped, all rules removed");
    return 0;
}

#include "netlink.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <linux/pkt_cls.h>
#include <linux/tc_act/tc_mirred.h>
#include <linux/if_ether.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace nre {
namespace {

constexpr uint32_t kIngressHandle = 0xffff0000u;
constexpr uint32_t kRootHandle    = 0x00010000u;
constexpr uint32_t kNetemLimit    = 10000; // packets; large enough for delay*rate

inline size_t align4(size_t n) { return (n + 3u) & ~size_t(3); }

// Tiny netlink message builder.
class Msg {
public:
    Msg(uint16_t type, uint16_t flags) : buf_(NLMSG_HDRLEN, 0) {
        auto *h = reinterpret_cast<nlmsghdr *>(buf_.data());
        h->nlmsg_type = type;
        h->nlmsg_flags = flags;
    }

    template <class T> void payload(const T &v) { raw(&v, sizeof(v)); }

    void raw(const void *d, size_t n) {
        size_t off = buf_.size();
        buf_.resize(off + align4(n), 0);
        if (n) std::memcpy(buf_.data() + off, d, n);
    }

    void attr(uint16_t type, const void *d, size_t n) {
        size_t off = buf_.size();
        buf_.resize(off + align4(RTA_LENGTH(n)), 0);
        auto *a = reinterpret_cast<rtattr *>(buf_.data() + off);
        a->rta_type = type;
        a->rta_len = static_cast<unsigned short>(RTA_LENGTH(n));
        if (n) std::memcpy(buf_.data() + off + RTA_LENGTH(0), d, n);
    }

    void attrStr(uint16_t type, const char *s) { attr(type, s, std::strlen(s) + 1); }
    void attrU32(uint16_t type, uint32_t v)    { attr(type, &v, sizeof(v)); }
    template <class T> void attrOf(uint16_t type, const T &v) { attr(type, &v, sizeof(v)); }

    size_t nestBegin(uint16_t type) {
        size_t off = buf_.size();
        attr(type, nullptr, 0);
        return off;
    }
    void nestEnd(size_t off) {
        auto *a = reinterpret_cast<rtattr *>(buf_.data() + off);
        a->rta_len = static_cast<unsigned short>(buf_.size() - off);
    }

    std::vector<uint8_t> finish() {
        auto *h = reinterpret_cast<nlmsghdr *>(buf_.data());
        h->nlmsg_len = static_cast<uint32_t>(buf_.size());
        return std::move(buf_);
    }

private:
    std::vector<uint8_t> buf_;
};

constexpr uint16_t kReq    = NLM_F_REQUEST | NLM_F_ACK;
constexpr uint16_t kCreate = kReq | NLM_F_CREATE | NLM_F_EXCL;
constexpr uint16_t kReplace = kReq | NLM_F_CREATE | NLM_F_REPLACE;

tcmsg makeTc(int ifindex, uint32_t handle, uint32_t parent, uint32_t info = 0) {
    tcmsg t{};
    t.tcm_family = AF_UNSPEC;
    t.tcm_ifindex = ifindex;
    t.tcm_handle = handle;
    t.tcm_parent = parent;
    t.tcm_info = info;
    return t;
}

} // namespace

// ---------------------------------------------------------------- Netlink

Netlink::Netlink() {
    fd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd_ < 0) return;

    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    if (::bind(fd_, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    timeval tv{5, 0};
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

Netlink::~Netlink() {
    if (fd_ >= 0) ::close(fd_);
}

int Netlink::request(std::vector<uint8_t> msg) {
    if (fd_ < 0) return -EBADF;

    auto *h = reinterpret_cast<nlmsghdr *>(msg.data());
    h->nlmsg_seq = ++seq_;

    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    if (::sendto(fd_, msg.data(), msg.size(), 0,
                 reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) < 0)
        return -errno;

    uint8_t buf[8192];
    for (;;) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        int len = static_cast<int>(n);
        for (auto *r = reinterpret_cast<nlmsghdr *>(buf); NLMSG_OK(r, len);
             r = NLMSG_NEXT(r, len)) {
            if (r->nlmsg_seq != seq_) continue;
            if (r->nlmsg_type == NLMSG_ERROR)
                return reinterpret_cast<nlmsgerr *>(NLMSG_DATA(r))->error;
            if (r->nlmsg_type == NLMSG_DONE) return 0;
        }
    }
}

std::vector<uint8_t> Netlink::buildNetem(int ifindex, const NetemParams &p) {
    Msg m(RTM_NEWQDISC, kReplace);
    m.payload(makeTc(ifindex, kRootHandle, TC_H_ROOT));
    m.attrStr(TCA_KIND, "netem");

    tc_netem_qopt qopt{};
    qopt.limit = kNetemLimit;
    double loss = std::min(std::max(p.loss_pct, 0.0), 100.0);
    qopt.loss = static_cast<uint32_t>(std::llround(loss / 100.0 * 4294967295.0));

    size_t opt = m.nestBegin(TCA_OPTIONS);
    m.payload(qopt);

    // 64-bit nanosecond delay/jitter (kernel prefers these over the legacy ticks)
    int64_t lat = static_cast<int64_t>(std::llround(std::max(p.latency_ms, 0.0) * 1e6));
    int64_t jit = (p.latency_ms > 0)
                      ? static_cast<int64_t>(std::llround(std::max(p.jitter_ms, 0.0) * 1e6))
                      : 0;
    m.attrOf(TCA_NETEM_LATENCY64, lat);
    m.attrOf(TCA_NETEM_JITTER64, jit);

    if (p.rate_mbit > 0) {
        uint64_t bytes = static_cast<uint64_t>(std::llround(p.rate_mbit * 1e6 / 8.0));
        if (bytes == 0) bytes = 1;
        tc_netem_rate r{};
        r.rate = bytes >= (1ull << 32) ? ~0u : static_cast<uint32_t>(bytes);
        m.attrOf(TCA_NETEM_RATE, r);
        if (bytes >= (1ull << 32)) m.attrOf(TCA_NETEM_RATE64, bytes);
    }
    m.nestEnd(opt);
    return m.finish();
}

int Netlink::setNetem(int ifindex, const NetemParams &p) {
    return request(buildNetem(ifindex, p));
}

int Netlink::clearRoot(int ifindex) {
    Msg m(RTM_DELQDISC, kReq);
    m.payload(makeTc(ifindex, 0, TC_H_ROOT));
    return request(m.finish());
}

int Netlink::clearIngress(int ifindex) {
    Msg m(RTM_DELQDISC, kReq);
    m.payload(makeTc(ifindex, kIngressHandle, TC_H_INGRESS));
    return request(m.finish());
}

int Netlink::setIngressRedirect(int ifindex, int ifbIndex) {
    // Start clean so repeated calls never stack duplicate filters.
    clearIngress(ifindex);

    // 1) ingress qdisc
    {
        Msg m(RTM_NEWQDISC, kReplace);
        m.payload(makeTc(ifindex, kIngressHandle, TC_H_INGRESS));
        m.attrStr(TCA_KIND, "ingress");
        int r = request(m.finish());
        if (r < 0) return r;
    }

    // 2) match-all u32 filter with a mirred "egress redirect" action to the ifb
    Msg m(RTM_NEWTFILTER, kCreate);
    m.payload(makeTc(ifindex, 0, kIngressHandle,
                     TC_H_MAKE(1u << 16, htons(ETH_P_ALL))));
    m.attrStr(TCA_KIND, "u32");

    size_t opt = m.nestBegin(TCA_OPTIONS);

    tc_u32_sel sel{};
    sel.flags = TC_U32_TERMINAL;
    sel.nkeys = 1;
    tc_u32_key key{}; // mask 0, val 0, off 0 -> matches every packet
    uint8_t selbuf[sizeof(sel) + sizeof(key)];
    std::memcpy(selbuf, &sel, sizeof(sel));
    std::memcpy(selbuf + sizeof(sel), &key, sizeof(key));
    m.attr(TCA_U32_SEL, selbuf, sizeof(selbuf));

    size_t acts = m.nestBegin(TCA_U32_ACT);
    size_t act1 = m.nestBegin(1);
    m.attrStr(TCA_ACT_KIND, "mirred");
    size_t aopt = m.nestBegin(TCA_ACT_OPTIONS);
    tc_mirred mir{};
    mir.action = TC_ACT_STOLEN;
    mir.eaction = TCA_EGRESS_REDIR;
    mir.ifindex = static_cast<__u32>(ifbIndex);
    m.attrOf(TCA_MIRRED_PARMS, mir);
    m.nestEnd(aopt);
    m.nestEnd(act1);
    m.nestEnd(acts);

    m.nestEnd(opt);
    return request(m.finish());
}

int Netlink::createIfb(const std::string &name, int &ifindex) {
    {
        Msg m(RTM_NEWLINK, kCreate);
        ifinfomsg i{};
        i.ifi_family = AF_UNSPEC;
        m.payload(i);
        m.attrStr(IFLA_IFNAME, name.c_str());
        size_t li = m.nestBegin(IFLA_LINKINFO);
        m.attrStr(IFLA_INFO_KIND, "ifb");
        m.nestEnd(li);
        int r = request(m.finish());
        if (r < 0 && r != -EEXIST) return r;
    }

    ifindex = static_cast<int>(::if_nametoindex(name.c_str()));
    if (ifindex == 0) return -ENODEV;

    Msg m(RTM_NEWLINK, kReq);
    ifinfomsg i{};
    i.ifi_family = AF_UNSPEC;
    i.ifi_index = ifindex;
    i.ifi_flags = IFF_UP;
    i.ifi_change = IFF_UP;
    m.payload(i);
    return request(m.finish());
}

int Netlink::deleteLink(int ifindex) {
    Msg m(RTM_DELLINK, kReq);
    ifinfomsg i{};
    i.ifi_family = AF_UNSPEC;
    i.ifi_index = ifindex;
    m.payload(i);
    return request(m.finish());
}

// ------------------------------------------------------------ LinkMonitor

LinkMonitor::LinkMonitor() {
    fd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (fd_ < 0) return;
    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK;
    if (::bind(fd_, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) < 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

LinkMonitor::~LinkMonitor() {
    if (fd_ >= 0) ::close(fd_);
}

bool LinkMonitor::dispatch(const Callback &cb) {
    bool lost = false;
    uint8_t buf[16384];
    for (;;) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == ENOBUFS) { lost = true; continue; }
            break; // EAGAIN or real error
        }
        int len = static_cast<int>(n);
        for (auto *h = reinterpret_cast<nlmsghdr *>(buf); NLMSG_OK(h, len);
             h = NLMSG_NEXT(h, len)) {
            if (h->nlmsg_type != RTM_NEWLINK && h->nlmsg_type != RTM_DELLINK) continue;
            auto *i = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(h));
            int alen = static_cast<int>(IFLA_PAYLOAD(h));
            std::string name;
            for (auto *a = IFLA_RTA(i); RTA_OK(a, alen); a = RTA_NEXT(a, alen))
                if (a->rta_type == IFLA_IFNAME)
                    name.assign(static_cast<const char *>(RTA_DATA(a)));
            if (!name.empty())
                cb(name, i->ifi_index, h->nlmsg_type == RTM_DELLINK);
        }
    }
    return lost;
}

} // namespace nre

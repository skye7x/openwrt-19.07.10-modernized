// NREPlatform LiteV - minimal rtnetlink helper (qdiscs, filters, ifb links)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nre {

struct NetemParams {
    double latency_ms = 0;   // added delay
    double jitter_ms  = 0;   // delay variation (only with latency)
    double loss_pct   = 0;   // packet loss, 0..100
    double rate_mbit  = 0;   // max speed in Mbit/s, 0 = unlimited

    bool any() const { return latency_ms > 0 || loss_pct > 0 || rate_mbit > 0; }
};

// All methods return 0 on success or a negative errno value.
class Netlink {
public:
    Netlink();
    ~Netlink();
    Netlink(const Netlink &) = delete;
    Netlink &operator=(const Netlink &) = delete;

    bool ok() const { return fd_ >= 0; }

    int setNetem(int ifindex, const NetemParams &p);      // root qdisc
    int clearRoot(int ifindex);
    int setIngressRedirect(int ifindex, int ifbIndex);    // ingress qdisc + u32 -> mirred
    int clearIngress(int ifindex);
    int createIfb(const std::string &name, int &ifindex); // also brings it up
    int deleteLink(int ifindex);

    // Exposed so the message layout can be unit-tested without a kernel.
    static std::vector<uint8_t> buildNetem(int ifindex, const NetemParams &p);

private:
    int request(std::vector<uint8_t> msg);
    int fd_ = -1;
    uint32_t seq_ = 0;
};

// Listens for link add/remove events (interfaces appearing/disappearing).
class LinkMonitor {
public:
    LinkMonitor();
    ~LinkMonitor();
    LinkMonitor(const LinkMonitor &) = delete;
    LinkMonitor &operator=(const LinkMonitor &) = delete;

    int fd() const { return fd_; }

    // callback(name, ifindex, removed). Returns true if events were lost
    // (socket overflow) and the caller should resynchronise.
    using Callback = std::function<void(const std::string &, int, bool)>;
    bool dispatch(const Callback &cb);

private:
    int fd_ = -1;
};

} // namespace nre

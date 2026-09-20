// NREPlatform LiteV - reads /etc/config/nreplatform through libuci
#pragma once

#include "netlink.hpp"

#include <string>
#include <vector>

namespace nre {

enum class Direction { Egress, Ingress, Both };

struct Rule {
    std::string section;
    std::string device;
    Direction   dir = Direction::Both;
    NetemParams params;
};

// Loads all enabled 'port' sections. Returns false and fills err on failure.
bool loadRules(const std::string &package, const std::string &confdir,
               std::vector<Rule> &out, std::string &err);

const char *directionName(Direction d);

} // namespace nre

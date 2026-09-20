#include "config.hpp"

#include <uci.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nre {
namespace {

const char *opt(uci_context *ctx, uci_section *s, const char *name) {
    return uci_lookup_option_string(ctx, s, name);
}

double number(uci_context *ctx, uci_section *s, const char *name, double lo, double hi) {
    const char *v = opt(ctx, s, name);
    if (!v || !*v) return 0;
    char *end = nullptr;
    double d = std::strtod(v, &end);
    if (end == v || *end != '\0') return 0; // not a number -> treat as unset
    return std::min(std::max(d, lo), hi);
}

bool truthy(const char *v, bool def) {
    if (!v) return def;
    return !(std::strcmp(v, "0") == 0 || std::strcmp(v, "false") == 0 ||
             std::strcmp(v, "off") == 0 || std::strcmp(v, "no") == 0 ||
             std::strcmp(v, "disabled") == 0);
}

} // namespace

const char *directionName(Direction d) {
    switch (d) {
    case Direction::Egress:  return "egress";
    case Direction::Ingress: return "ingress";
    default:                 return "both";
    }
}

bool loadRules(const std::string &package, const std::string &confdir,
               std::vector<Rule> &out, std::string &err) {
    out.clear();

    uci_context *ctx = uci_alloc_context();
    if (!ctx) {
        err = "out of memory";
        return false;
    }
    if (!confdir.empty()) uci_set_confdir(ctx, confdir.c_str());

    uci_package *pkg = nullptr;
    if (uci_load(ctx, package.c_str(), &pkg) != UCI_OK || !pkg) {
        char *msg = nullptr;
        uci_get_errorstr(ctx, &msg, package.c_str());
        err = msg ? msg : "cannot load config";
        std::free(msg);
        uci_free_context(ctx);
        return false;
    }

    uci_element *e;
    uci_foreach_element(&pkg->sections, e) {
        uci_section *s = uci_to_section(e);
        if (std::strcmp(s->type, "port") != 0) continue;
        if (!truthy(opt(ctx, s, "enabled"), true)) continue;

        const char *dev = opt(ctx, s, "device");
        if (!dev || !*dev) continue;

        Rule r;
        r.section = s->e.name ? s->e.name : "";
        r.device = dev;

        const char *dir = opt(ctx, s, "direction");
        if (dir && std::strcmp(dir, "egress") == 0)       r.dir = Direction::Egress;
        else if (dir && std::strcmp(dir, "ingress") == 0) r.dir = Direction::Ingress;
        else                                               r.dir = Direction::Both;

        r.params.latency_ms = number(ctx, s, "latency", 0, 60000);
        r.params.jitter_ms  = number(ctx, s, "jitter", 0, 60000);
        r.params.loss_pct   = number(ctx, s, "loss", 0, 100);
        r.params.rate_mbit  = number(ctx, s, "rate", 0, 1000000);
        out.push_back(std::move(r));
    }

    uci_unload(ctx, pkg);
    uci_free_context(ctx);
    return true;
}

} // namespace nre

// SPDX-License-Identifier: LGPL-3.0-only
#ifndef IP2UNIX_RULES_HH
#define IP2UNIX_RULES_HH

#include <iostream>
#include <optional>
#include <vector>

#include <netinet/in.h>

#include "types.hh"

enum class RuleDir { INCOMING, OUTGOING };

struct Rule {
    std::optional<RuleDir> direction = std::nullopt;
    std::optional<SocketType> type = std::nullopt;
    std::optional<std::string> address = std::nullopt;
    std::optional<uint16_t> port = std::nullopt;

#ifdef SOCKET_ACTIVATION
    bool socket_activation = false;
#ifndef NO_FDNAMES
    std::optional<std::string> fd_name = std::nullopt;
#endif
#endif

    std::optional<std::string> socket_path = std::nullopt;

    bool reject = false;
    std::optional<int> reject_errno = std::nullopt;

    bool blackhole = false;
};

std::optional<std::vector<Rule>> parse_rules(std::string, bool);
std::optional<Rule> parse_rule_arg(size_t, const std::string&);
std::string encode_rules(std::vector<Rule>);
void print_rules(std::vector<Rule>&, std::ostream&);

#ifdef SOCKET_ACTIVATION
std::optional<int> get_systemd_fd_for_rule(const Rule&);
#endif

bool match_sockaddr_in(const struct sockaddr_in*, Rule);

#endif

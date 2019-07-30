// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the syntax logic for the packet filter language.

#ifndef SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_INTERNAL_H_
#define SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_INTERNAL_H_

#include <arpa/inet.h>
#include <zircon/assert.h>

#include <sstream>

#include "filter_builder.h"
#include "parser_state.h"

namespace netdump::parser {

constexpr auto ERROR_EXPECTED_ETH_FIELD = "Expected: one of {'proto', 'dst', 'src', 'host'}.";
constexpr auto ERROR_EXPECTED_ETH_TYPE = "Expected: one of {'arp', 'ip', 'ip6', 'vlan'}.";
constexpr auto ERROR_EXPECTED_HEX = "Hex value expected.";
constexpr auto ERROR_EXPECTED_HOST = "Expected: 'host'.";
constexpr auto ERROR_EXPECTED_PORT = "Expected: 'port'.";
constexpr auto ERROR_EXPECTED_IP_ADDR = "IP address expected.";

// These errors are for when the IP address provided is inconsistent with the version specified.
// E.g. `ip6 host 192.168.1.1`.
constexpr auto ERROR_EXPECTED_IPV4_GOT_IPV6 = "IPv4 address expected, got IPv6.";
constexpr auto ERROR_EXPECTED_IPV6_GOT_IPV4 = "IPv6 address expected, got IPv4.";

constexpr auto ERROR_EXPECTED_LENGTH = "Length value expected.";
constexpr auto ERROR_EXPECTED_MAC = "MAC address expected.";
constexpr auto ERROR_EXPECTED_PORT_VALUE = "Port value expected.";
constexpr auto ERROR_EXPECTED_TRANSPORT = "Expected: one of {'tcp', 'udp', 'icmp'}.";
constexpr auto ERROR_INVALID_LENGTH = "Invalid length value.";
constexpr auto ERROR_INVALID_PORT = "Invalid port value:";
constexpr auto ERROR_MAC_LENGTH = "Wrong number of digits for MAC address.";
constexpr auto ERROR_REQUIRED_CONNECTIVE = "Logical connective required.";
constexpr auto ERROR_UNEXPECTED_R_PARENS = "Unexpected ')'.";
constexpr auto ERROR_UNEXPECTED_CONNECTIVE = "Unexpected logical connective.";
constexpr auto ERROR_UNKNOWN_KEYWORD = "Unknown keyword.";
constexpr auto ERROR_UNMATCHED_L_PARENS = "Parenthesis without matching ')'.";

template <class T>
class Syntax {
#define TOKEN (**env_)
#define ENV (*env_)
 public:
  Syntax(const Tokenizer& tkz, Environment* env, FilterBuilder<T>* bld)
      : tkz_(tkz), env_(env), bld_(bld), failed_(false), parens_(0) {}

  // Attempt a parse by recursive descent. The parse state is tracked in `env`.
  // Return null if the specification is invalid. On return, the `env` error data is updated if
  // there was a syntax mistake.
  std::optional<T> parse() {
    OptT filter{};
    ParseOpState state{};  // Need a new one for every parenthesis level.

    for (auto prev = ENV.begin(); !(ENV.at_end() || failed_); prev = ENV.cur()) {
      if (try_consume(tkz_.L_PARENS)) {
        ++parens_;
        try_parse(&Syntax::parse, &filter, &state);
        --parens_;
      }
      if (!ENV.at_end() && TOKEN == tkz_.R_PARENS) {
        if (parens_ > 0 && filter != std::nullopt && state.op == ParseOp::NONE &&
            state.negations == 0) {
          // End of current level of parenthesis. Return to the level above.
          ++ENV;
          return filter;
        }
        // Unmatched right parenthesis.
        return set_failed(ERROR_UNEXPECTED_R_PARENS);
      }
      if (!ENV.at_end() && try_consume(tkz_.NOT)) {
        ++state.negations;
      }
      if (!ENV.at_end() && TOKEN->one_of(tkz_.OR, tkz_.AND)) {
        if (filter == std::nullopt || state.op != ParseOp::NONE || state.negations > 0) {
          return set_failed(ERROR_UNEXPECTED_CONNECTIVE);
        }
        state.op = (TOKEN == tkz_.OR ? ParseOp::DISJ : ParseOp::CONJ);
        ++ENV;
      }

      // Try each type of expression in turn.
      try_parse(&Syntax::frame_length_expr, &filter, &state);
      try_parse(&Syntax::eth_expr, &filter, &state);
      try_parse(&Syntax::host_expr, &filter, &state);
      try_parse(&Syntax::trans_expr, &filter, &state);

      if (failed_ && ENV.error_loc == std::nullopt) {
        // If error location is not set on failure, the error happened at `prev`.
        ENV.error_loc = prev;
      }
      if ((!failed_) && prev == ENV.cur()) {
        // Did not make progress, and yet did not fail. This is an unknown token.
        return set_failed(ERROR_UNKNOWN_KEYWORD);
      }
    }
    if (failed_) {
      return std::nullopt;
    }
    // A few extra syntax error conditions at the end of the current parenthesis.
    if (parens_ > 0) {
      ENV.error_cause = ERROR_UNMATCHED_L_PARENS;
      // Not setting error location since we want to point to the open parenthesis,
      // instead of here.
      failed_ = true;
      return std::nullopt;
    }
    if (state.op != ParseOp::NONE || state.negations > 0) {
      --ENV;
      return set_failed(ERROR_UNEXPECTED_CONNECTIVE);
    }
    return filter;
  }

 private:
  // `T` is type of filter constructed.
  using OptT = std::optional<T>;

  // Return the current token and advance the token cursor.
  inline TokenPtr consume() {
    TokenPtr result = TOKEN;
    ++ENV;
    return result;
  }

  // If the current token is one of those given as input, return true and advance the token cursor.
  template <typename... Ts>
  inline bool try_consume(TokenPtr tok, Ts... ts) {
    if (TOKEN->one_of(tok, ts...)) {
      ++ENV;
      return true;
    }
    return false;
  }

  // Attempt to parse a filter using the `fn` function. Combine its result with the `current`
  // filter.
  inline void try_parse(OptT (Syntax::*fn)(), OptT* current, ParseOpState* state) {
    ZX_ASSERT_MSG(state != nullptr, "Got nullptr for ParseOp state.");
    if (failed_) {
      return;
    }
    OptT parsed = (this->*fn)();
    if (parsed == std::nullopt) {
      return;
    }
    *current = create_filter(std::move(*current), std::move(parsed), state);
  }

  // Returns the negation of `filter` if the negation count is odd, or `filter` if it is even.
  // The negation count is reset to 0.
  inline OptT negate_filter(OptT filter, ParseOpState* state) {
    if ((state->negations) & 1) {
      state->negations = 0;
      return (filter == std::nullopt ? std::nullopt
                                     : std::optional(bld_->negation(std::move(*filter))));
    }
    state->negations = 0;
    return filter;
  }

  // Returns the logical composition of `left` and `right` filters with the `ParseOpState` op.
  // `op` is reset to `NONE` if `left` and `right` were composed.
  OptT compose_filters(OptT left, OptT right, ParseOpState* state) {
    if (right == std::nullopt) {
      return OptT{};
    }
    switch (state->op) {
      case ParseOp::NONE:
        if (left == std::nullopt) {
          // Initial state where `left` is null.
          return std::move(right);
        }
        // `left != nullopt` happens on a syntax error: two filters were juxtaposed
        // with no logical connective.
        ENV.error_cause = ERROR_REQUIRED_CONNECTIVE;
        failed_ = true;
        // Not setting `error_loc` as mistake actually happened at an earlier point.
        return OptT{};
      case ParseOp::CONJ:
        state->op = ParseOp::NONE;
        return bld_->conjunction(std::move(*left), std::move(*right));
      case ParseOp::DISJ:
        state->op = ParseOp::NONE;
        return bld_->disjunction(std::move(*left), std::move(*right));
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unexpected ParseOp state.");
        return OptT{};
    }
  }

  // Once the parsed filter has been constructed, compose it with the current filter with
  // operations given in `ParseOpState`.
  inline OptT create_filter(OptT current, OptT parsed, ParseOpState* state) {
    parsed = negate_filter(std::move(parsed), state);
    return compose_filters(std::move(current), std::move(parsed), state);
  }

  // Set the error cause and location in the parse environment, if no failure already.
  // Returns null so callers can simply return on the result of calling this function.
  inline OptT set_failed(const char* cause, TokenIterator loc) {
    if (!failed_) {
      ENV.error_cause = cause;
      ENV.error_loc = loc;
      failed_ = true;
    }
    return std::nullopt;
  }

  // Helper overload that sets the error location to the current location in the environment.
  inline OptT set_failed(const char* cause) { return set_failed(cause, ENV.cur()); }

  // Helper for constructing length filters.
  std::optional<uint16_t> length_value() {
    if (ENV.at_end()) {
      set_failed(ERROR_EXPECTED_LENGTH);
      return std::nullopt;  // Must return null explicitly for the right type.
    }
    size_t num_end;
    std::string input = TOKEN->get_term();
    long int num = stol(input, &num_end, 10);  // Base-10 number.
    if (num < 0 || num_end < input.length()) {
      set_failed(ERROR_INVALID_LENGTH);
      return std::nullopt;
    }
    ++ENV;
    return static_cast<uint16_t>(num);
  }

  OptT frame_length_expr() {
    if (ENV.at_end() || !TOKEN->one_of(tkz_.LESS, tkz_.GREATER)) {
      return std::nullopt;
    }
    TokenPtr comparator = consume();
    std::optional<uint16_t> length = length_value();
    if (length == std::nullopt) {
      return std::nullopt;
    }
    return bld_->frame_length(*length, comparator);
  }

  OptT eth_expr() {
    if (ENV.at_end()) {
      return std::nullopt;
    }
    if (!try_consume(tkz_.ETHER)) {
      return net_expr();
    }
    if (ENV.at_end()) {
      return set_failed(ERROR_EXPECTED_ETH_FIELD);
    }
    if (try_consume(tkz_.HOST)) {
      return mac_expr(tkz_.HOST);
    }
    if (TOKEN->one_of(tkz_.DST, tkz_.SRC)) {
      TokenPtr type = consume();
      if (ENV.at_end() || !try_consume(tkz_.HOST)) {
        return set_failed(ERROR_EXPECTED_HOST);
      }
      return mac_expr(type);
    }
    if (try_consume(tkz_.PROTO)) {
      OptT filter = net_expr();  // Must succeed;
      if (filter != std::nullopt) {
        return filter;
      }
      return set_failed(ERROR_EXPECTED_ETH_TYPE);
    }
    return set_failed(ERROR_EXPECTED_ETH_FIELD);
  }

  OptT mac_expr(TokenPtr type) {
    if (ENV.at_end()) {
      return set_failed(ERROR_EXPECTED_MAC);
    }

    std::string mac_str = TOKEN->get_term();
    // Erase all delimiters.
    mac_str.erase(std::remove(mac_str.begin(), mac_str.end(), ':'), mac_str.end());

    if (mac_str.length() != ETH_ALEN * 2) {
      return set_failed(ERROR_MAC_LENGTH);
    }

    std::array<uint8_t, ETH_ALEN> mac;
    std::array<unsigned int, ETH_ALEN> tmp;
    size_t mac_length = std::sscanf(mac_str.c_str(), "%02x%02x%02x%02x%02x%02x", &tmp[5], &tmp[4],
                                    &tmp[3], &tmp[2], &tmp[1], &tmp[0]);
    if (mac_length < ETH_ALEN) {
      return set_failed(ERROR_EXPECTED_HEX);  // Some non-hex characters were found.
    }
    std::transform(tmp.begin(), tmp.end(), mac.begin(),
                   [](unsigned int octet) { return static_cast<uint8_t>(octet); });

    ++ENV;
    return bld_->mac(mac, type);
  }

  OptT net_expr() {
    if (ENV.at_end()) {
      return std::nullopt;
    }
    if (TOKEN->one_of(tkz_.ARP, tkz_.VLAN)) {
      TokenPtr etype = consume();
      return bld_->ethertype(etype->get_tag<uint16_t>());
    }
    if (TOKEN->one_of(tkz_.IP, tkz_.IP6)) {
      TokenPtr ip = consume();
      auto version = std::optional(ip->get_tag<uint8_t>());
      if (ENV.at_end()) {
        return bld_->ip_version(*version);
      }
      OptT filter = ip_pkt_length_expr(version);
      if (failed_ || filter != std::nullopt) {
        return filter;
      }
      filter = host_expr(version);
      if (failed_ || filter != std::nullopt) {
        return filter;
      }
      filter = trans_expr(version);
      if (failed_ || filter != std::nullopt) {
        return filter;
      }
      return bld_->ip_version(*version);
    }
    return std::nullopt;
  }

  // `ip_ver` can be either 4, 6, or null if the version was unspecified.
  // Check it is one of these and return true if the version was specified.
  inline bool has_ip_ver(std::optional<uint8_t> ip_ver) {
    if (ip_ver == std::nullopt) {
      return false;
    }
    ZX_ASSERT_MSG(*ip_ver == 4 || *ip_ver == 6, "Unsupported IP version: %u", *ip_ver);
    return true;
  }

  OptT ip_pkt_length_expr(std::optional<uint8_t> ip_ver) {
    ZX_ASSERT_MSG(has_ip_ver(ip_ver), "An IP version must be specified for IP packet length.");
    if (ENV.at_end() || !TOKEN->one_of(tkz_.GREATER, tkz_.LESS)) {
      return std::nullopt;
    }
    TokenPtr comparator = consume();
    std::optional<uint16_t> length = length_value();
    if (length == std::nullopt) {
      return std::nullopt;
    }
    return bld_->ip_pkt_length(*ip_ver, *length, comparator);
  }

  OptT host_expr() { return host_expr(std::nullopt); }
  OptT host_expr(std::optional<uint8_t> ip_ver) {
    if (ENV.at_end() || !TOKEN->one_of(tkz_.HOST, tkz_.DST, tkz_.SRC)) {
      return std::nullopt;
    }
    TokenPtr type = consume();
    if (type->one_of(tkz_.DST, tkz_.SRC)) {
      if (ENV.at_end()) {
        return set_failed(ERROR_EXPECTED_HOST);
      }
      if (!try_consume(tkz_.HOST)) {
        // This may be some other expression instead, e.g. port expression.
        --ENV;
        return std::nullopt;
      }
    }
    if (ENV.at_end()) {
      return set_failed(ERROR_EXPECTED_IP_ADDR);
    }
    std::string addrstr = TOKEN->get_term();

    bool any_ip_ver = !has_ip_ver(ip_ver);
    // Try parsing the IP address for IPv4 and IPv6, and see if it is consistent with `ip_ver`.
    uint32_t ip4addr = 0;
    if (inet_pton(AF_INET, addrstr.c_str(), &ip4addr)) {
      if (any_ip_ver || *ip_ver == 4) {
        ++ENV;
        return bld_->ipv4_address(ntohl(ip4addr), type);
      }
      return set_failed(ERROR_EXPECTED_IPV6_GOT_IPV4);
    }

    std::array<uint8_t, IP6_ADDR_LEN> ip6addr;
    if (inet_pton(AF_INET6, addrstr.c_str(), ip6addr.data())) {
      if (any_ip_ver || *ip_ver == 6) {
        ++ENV;
        std::reverse(ip6addr.begin(), ip6addr.end());  // Put in host byte order.
        return bld_->ipv6_address(ip6addr, type);
      }
      return set_failed(ERROR_EXPECTED_IPV4_GOT_IPV6);
    }

    return set_failed(ERROR_EXPECTED_IP_ADDR);
  }

  inline T trans_proto_filter(std::optional<uint8_t> ip_ver, uint8_t proto4, uint8_t proto6) {
    if (has_ip_ver(ip_ver)) {
      return bld_->ip_protocol(*ip_ver, (*ip_ver == 4) ? proto4 : proto6);
    }
    // Call the filter builder functions in an explicit evaluation order.
    T proto_filter4 = bld_->ip_protocol(4, proto4);
    T proto_filter6 = bld_->ip_protocol(6, proto6);
    return bld_->disjunction(std::move(proto_filter4), std::move(proto_filter6));
  }

  OptT trans_expr() { return trans_expr(std::nullopt); }
  OptT trans_expr(std::optional<uint8_t> ip_ver) {
    if (ENV.at_end()) {
      return std::nullopt;
    }
    bool proto_token = false;
    if (try_consume(tkz_.PROTO)) {
      // Once `proto_token` is set to true, we must get a transport protocol.
      if (ENV.at_end()) {
        return set_failed(ERROR_EXPECTED_TRANSPORT);
      }
      proto_token = true;
    }
    if (try_consume(tkz_.ICMP)) {
      return trans_proto_filter(ip_ver, IPPROTO_ICMP, IPPROTO_ICMPV6);
    }
    OptT proto_filter{};
    if (TOKEN->one_of(tkz_.TCP, tkz_.UDP)) {
      TokenPtr proto = consume();
      auto proto_num = proto->get_tag<uint8_t>();
      proto_filter = trans_proto_filter(ip_ver, proto_num, proto_num);
    }
    if (proto_filter == std::nullopt && proto_token) {
      return set_failed(ERROR_EXPECTED_TRANSPORT);
    }
    // There may still be a `port_expr` to come.
    OptT port_filter = port_expr();
    if (port_filter == std::nullopt) {
      return proto_filter;
    }
    if (proto_filter == std::nullopt) {
      return wrap_port_filter(ip_ver, std::move(port_filter));
    }
    return bld_->conjunction(std::move(*proto_filter), std::move(*port_filter));
  }

  // Conjoin `port_filter` with an IP version filter as necessary.
  inline OptT wrap_port_filter(std::optional<uint8_t> ip_ver, OptT port_filter) {
    if (has_ip_ver(ip_ver)) {
      return bld_->conjunction(bld_->ip_version(*ip_ver), std::move(*port_filter));
    }
    return port_filter;
  }

  inline OptT invalid_port(TokenPtr port) {
    std::stringstream stream;
    stream << ERROR_INVALID_PORT << " '" + port->get_term() << "'.";
    return set_failed(stream.str().c_str());
  }

  OptT process_ports(const std::vector<TokenPtr>& ports, TokenPtr type) {
    bool unrecognized = false;
    std::vector<PortRange> ranges;
    FunctionalTokenVisitor visitor(
        [&unrecognized](TokenPtr /*t*/) { unrecognized = true; },  // Not a port token.
        [&ranges](PortTokenPtr t) { ranges.emplace_back(t->begin(), t->end()); });
    for (const TokenPtr& port : ports) {
      port->accept(&visitor);
      if (unrecognized) {
        return invalid_port(port);
      }
    }
    ++ENV;
    return bld_->ports(std::move(ranges), type);
  }

  OptT port_expr() {
    if (ENV.at_end() || !TOKEN->one_of(tkz_.PORT, tkz_.SRC, tkz_.DST)) {
      return std::nullopt;
    }
    TokenPtr type = consume();
    if (type->one_of(tkz_.DST, tkz_.SRC)) {
      if (ENV.at_end() || !try_consume(tkz_.PORT)) {
        return set_failed(ERROR_EXPECTED_PORT);
      }
    }
    if (ENV.at_end()) {
      return set_failed(ERROR_EXPECTED_PORT_VALUE);
    }
    std::vector<TokenPtr> ports = tkz_.mult_ports(',', TOKEN->get_term());
    if (ports.empty()) {
      return set_failed(ERROR_EXPECTED_PORT_VALUE);
    }
    return process_ports(ports, type);
  }

  const Tokenizer& tkz_;
  Environment* env_;
  FilterBuilder<T>* bld_;
  // True once an error is encountered.
  bool failed_;
  // Count the depth of parentheses.
  size_t parens_;
#undef ENV
#undef TOKEN
};

}  // namespace netdump::parser

#endif  // SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_INTERNAL_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Lexing of tokens supported by the filter syntax.
// The goal of this lexer is to insulate the parser from the concrete characters in the input.
// Instead of raw characters, the input string is converted to tokens for parser consumption.
// This allows the parser to perform more efficient equality comparison based on token identity
// instead of string equality, as well as savings on space and a more extensible syntax.
//
// Tokens are instances of `TokenBase` and its subclasses. We manage tokens differently based on
// whether they represent keywords or literals. Keywords are reserved terms in the language, and
// their representing tokens are registered in a dictionary. Any attempt to mint a new token of a
// reserved term will instead obtain an existing keyword token. Otherwise, new tokens for literal,
// non-reserved terms can be created freely.
//
// Outside of this module, tokens should always be wrapped in `RefPtr`, to enforce identity
// uniqueness of keywords, as well as minimizing memory leaks. Token equivalence is deemed to
// equivalence of their wrapping `RefPtr`. For a client, tokens can only be constructed by a factory
// `Tokenizer`. This ensures tokens of keywords are properly registered for central lookup.
// Lifetime-wise, tokens representing keywords are owned by their creating `Tokenizer`, and clients
// borrow copies. Tokens representing literals (non-keywords) are vended by `Tokenizer` but not
// registered in dictionary, therefore ownership is taken by the client.
//
// Each keyword token has an optionally present metadata field `tag`. This allows the injection of a
// small amount of semantic meaning to the token that could simplify the parser's decision-making
// when dealing with tokens of the same semantic class. For example, the parser may treat
// identically all the tokens representing transport-layer protocols, and only forward the `tag`
// data to the filter. This will work if `tag` is filled with appropriate protocol numbers such as
// `IPPROTO_TCP`. Besides choosing appropriate values for the `tag` metadata, the lexer should
// otherwise not be involved in the semantic understanding of the tokens.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_TOKENS_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_TOKENS_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <zircon/boot/netboot.h>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "filter_constants.h"

namespace netdump {

// Base class of all tokens.
class TokenBase;
// A specialized token representing a port or a range of ports.
class PortToken;

using TokenPtr = fbl::RefPtr<TokenBase>;
using PortTokenPtr = fbl::RefPtr<PortToken>;

// A visitor that acts differently for the two classes of tokens when they are mixed in a container.
class TokenVisitor {
 public:
  virtual void visit(TokenPtr token) = 0;
  virtual void visit(PortTokenPtr token) = 0;
};

// An implementation of `TokenVisitor` with visit functions definable on construction.
class FunctionalTokenVisitor : public TokenVisitor {
 public:
  FunctionalTokenVisitor(std::function<void(TokenPtr)> token_fn,
                         std::function<void(PortTokenPtr)> port_token_fn)
      : token_fn_(std::move(token_fn)), port_token_fn_(std::move(port_token_fn)) {}
  void visit(TokenPtr token) override { token_fn_(token); }
  void visit(PortTokenPtr token) override { port_token_fn_(token); }

 private:
  std::function<void(TokenPtr)> token_fn_;
  std::function<void(PortTokenPtr)> port_token_fn_;
};

class TokenBase : public fbl::RefCounted<TokenBase> {
 public:
  // The string representation.
  [[nodiscard]] std::string get_term() const { return term_; }

  // Gets the numerical metadata tag that can help with parsing.
  template <class T>
  T get_tag() const {
    return static_cast<T>(tag_);
  }

  virtual ~TokenBase() = default;

  TokenBase(const TokenBase& other) = delete;
  TokenBase& operator=(const TokenBase&) = delete;

  // Passing `visitor` as pointer since `visit` is a non-const function and `visitor` may have
  // state that mutates on `visit`, but mutable references are not allowed.
  virtual void accept(TokenVisitor* visitor) { visitor->visit(fbl::RefPtr(this)); }

  // Returns `true` if the token is a member of the given set.
  // Instead of writing `token == a || token == b || token == c`,
  // write `token->one_of(a, b, c)`.
  [[nodiscard]] inline bool one_of(const TokenPtr& other) const {
    return fbl::RefPtr(this) == other;
  }

  template <typename... Ts>
  [[nodiscard]] inline bool one_of(const TokenPtr& other, Ts... ts) const {
    return fbl::RefPtr(this) == other || one_of(ts...);
  }

 protected:
  // Protect construction to limit who can create `TokenPtrs`.
  explicit TokenBase(std::string term, uint64_t tag) : term_(std::move(term)), tag_(tag) {}

 private:
  const std::string term_;
  const uint64_t tag_;

  friend class Tokenizer;
};

// Class of tokens expressing ports and port ranges.
// A `PortToken` holds a keyword that represents a named port, such as port 22 represented by "SSH".
// A port or range can also be represented as a numeric (e.g. "22") or range string (e.g. "10-20").
class PortToken : public TokenBase {
 public:
  static inline std::string port_term(uint16_t begin, uint16_t end) {
    return std::to_string(begin) + (begin == end ? "" : ("-" + std::to_string(end)));
  }

  [[nodiscard]] uint16_t begin() const { return begin_; }
  [[nodiscard]] uint16_t end() const { return end_; }

  PortToken(const PortToken& other) = delete;
  PortToken& operator=(const PortToken& other) = delete;

  void accept(TokenVisitor* visitor) override { visitor->visit(fbl::RefPtr(this)); }

 private:
  const uint16_t begin_;
  const uint16_t end_;

  PortToken(std::string term, uint16_t beginp, uint16_t endp, uint64_t tag)
      : TokenBase(std::move(term), tag), begin_(beginp), end_(endp) {}

  PortToken(uint16_t beginp, uint16_t endp, uint64_t tag)
      : TokenBase(port_term(beginp, endp), tag), begin_(beginp), end_(endp) {}

  friend class Tokenizer;
};

// Factory for producing TokenPtrs.
class Tokenizer {
 private:
  // `dictionary_` maps a keyword to a canonical token.
  // This must be initialized before the keywords.
  std::unordered_map<std::string, TokenPtr> dictionary_{};

 public:
  // List of keywords organized by category.

  // Logical operations, no `tag`.
  const TokenPtr L_PARENS = keyword("(");
  const TokenPtr R_PARENS = keyword(")");
  const TokenPtr NOT = keyword("not", "!");
  const TokenPtr AND = keyword("and", "&&");
  const TokenPtr OR = keyword("or", "^^");

  // Length comparison operations, `tag` is one of LengthComparator.
  const TokenPtr GREATER = keyword("greater", LengthComparator::GEQ);
  const TokenPtr LESS = keyword("less", LengthComparator::LEQ);

  // Fields that can be matched against. `tag` is type of field, if different types exist.
  const TokenPtr ETHER = keyword("ether");
  const TokenPtr PROTO = keyword("proto");
  const TokenPtr HOST = keyword("host", AddressFieldType::EITHER_ADDR);
  const TokenPtr SRC = keyword("src", AddressFieldType::SRC_ADDR);
  const TokenPtr DST = keyword("dst", AddressFieldType::DST_ADDR);
  const TokenPtr PORT = keyword("port", "portrange", PortFieldType::EITHER_PORT);

  // L2 protocols besides IP, `tag` is Ethernet II ethertype.
  const TokenPtr ARP = keyword("arp", ETH_P_ARP);
  const TokenPtr VLAN = keyword("vlan", ETH_P_8021Q);

  // Versions of IP, `tag` is 4 or 6.
  const TokenPtr IP = keyword("ip", "ip4", 4);
  const TokenPtr IP6 = keyword("ip6", 6);

  // L4 protocols, `tag` is protocol number.
  const TokenPtr TCP = keyword("tcp", IPPROTO_TCP);
  const TokenPtr UDP = keyword("udp", IPPROTO_UDP);

  // Other protocols that may require special handling.
  // For ICMP, parser needs to convert protocol number to `IPPROTO_ICMPV6` as appropriate.
  const TokenPtr ICMP = keyword("icmp", IPPROTO_ICMP);

  // Named ports. No `tag`, but specify port number or range.

  // Fuchsia ports.
  const TokenPtr DBGLOG = named_port("dbglog", DEBUGLOG_PORT, DEBUGLOG_PORT);
  const TokenPtr DBGACK = named_port("dbgack", DEBUGLOG_ACK_PORT, DEBUGLOG_ACK_PORT);

  // IANA-defined ports.
  const TokenPtr DHCP = named_port("dhcp", 67, 68);
  const TokenPtr DNS = named_port("dns", 53, 53);
  const TokenPtr ECHO = named_port("echo", 7, 7);
  const TokenPtr FTPXFER = named_port("ftpxfer", 20, 20);
  const TokenPtr FTPCTL = named_port("ftpctl", 21, 21);
  const TokenPtr HTTP = named_port("http", 80, 80);
  const TokenPtr HTTPS = named_port("https", 443, 443);
  const TokenPtr IRC = named_port("irc", 194, 194);
  const TokenPtr NTP = named_port("ntp", 123, 123);
  const TokenPtr SFTP = named_port("sftp", 115, 115);
  const TokenPtr SSH = named_port("ssh", 22, 22);
  const TokenPtr TELNET = named_port("telnet", 23, 23);
  const TokenPtr TFTP = named_port("tftp", 69, 69);

  // Attempt to create a new token for `term` input by the user. If `term` is in the dictionary
  // i.e. it is reserved, then the keyword token is returned. Otherwise, vend out a new literal
  // token whose ownership is passed to the caller.
  // No `tag` value is expected as it is only meaningful for keywords.
  [[nodiscard]] TokenPtr literal(const std::string& term) const;

  // Tokenize a string of multiple terms separated by whitespace.
  [[nodiscard]] std::vector<TokenPtr> tokenize(const std::string& filter_string) const;

  // Tokenize a single port or port range input by the user.
  // If the input is in the dictionary, return the keyword `TokenPtr`.
  // Otherwise, return a `PortTokenPtr` if `port_string` specifies a valid port or port range.
  // If no valid port is specified, return a literal `TokenPtr` containing `port_string`.
  // This last outcome is likely to be a syntax error, how it is handled is up to the client.
  [[nodiscard]] TokenPtr port(const std::string& port_string) const;

  // Tokenize a list of ports or port ranges input by the user separated by `delim`.
  // Results of calling `port` on each element in `ports_list` are collected in the result.
  [[nodiscard]] std::vector<TokenPtr> mult_ports(char delim, const std::string& ports_list) const;

  Tokenizer() = default;

  Tokenizer(const Tokenizer& other) = delete;
  Tokenizer& operator=(const Tokenizer& other) = delete;

 protected:
  // Return a `TokenPtr` that is a keyword with a single term.
  // The token is taken from the dictionary, or registered there if not already present.
  TokenPtr keyword(const std::string& term, uint64_t tag = 0);

  // For a keyword with dual terms (a synonym). `term` will be the canonical representation.
  TokenPtr keyword(const std::string& term, const std::string& synonym, uint64_t tag = 0);

  // If an entry for `name` is found in the dictionary, the associated token is returned.
  // Otherwise, return a named port token with the given `begin` and `end` ports and register
  // it in the dictionary.
  TokenPtr named_port(const std::string& name, uint16_t begin, uint16_t end, uint64_t tag = 0);

  // Same with a synonym for the port name.
  TokenPtr named_port(const std::string& name, const std::string& synonym, uint16_t begin,
                      uint16_t end, uint64_t tag = 0);
};

}  // namespace netdump

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_TOKENS_H_

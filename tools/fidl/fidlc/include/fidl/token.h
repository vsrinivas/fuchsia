// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TOKEN_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TOKEN_H_

#include <stdint.h>

#include <string_view>

#include "tools/fidl/fidlc/include/fidl/source_span.h"

namespace fidl {

// A Token represents a typed view into a source buffer. That is, it
// has a TokenKind, and it has a buffer representing the data
// corresponding to the token. No processing is done on the data:
// string or numeric literals aren't further parsed, identifiers
// uniqued, and so on.
class Token {
 public:
  enum Kind : uint8_t {
#define TOKEN(Name) k##Name,
#include "tools/fidl/fidlc/include/fidl/token_definitions.inc"
#undef TOKEN
  };

  // Each identifier token is assigned a subkind, which are defined below, or a sentinel value if
  // the token does not match one of the subkinds, to make it simple for the parser to either
  // consume a generic identifier or one that matches a specific subkind. This design makes it so
  // that there are no "reserved keywords" and any identifier can be defined by the user.
  enum Subkind : uint8_t {
    kNone = 0,
#define TOKEN_SUBKIND(Name, Spelling) k##Name,
#include "tools/fidl/fidlc/include/fidl/token_definitions.inc"
#undef TOKEN_SUBKIND
  };

  class KindAndSubkind {
   public:
    constexpr KindAndSubkind(Kind kind, Subkind subkind) : kind_(kind), subkind_(subkind) {}

    constexpr Kind kind() const { return kind_; }
    constexpr Subkind subkind() const { return subkind_; }
    constexpr uint16_t combined() const {
      return static_cast<uint16_t>(kind_) | static_cast<uint16_t>(subkind_ << 8);
    }

   private:
    Kind kind_;
    Subkind subkind_;
  };

  Token(SourceSpan previous_end, SourceSpan span, Kind kind, Subkind subkind)
      : previous_end_(previous_end),
        span_(span),
        kind_and_subkind_(KindAndSubkind(kind, subkind)) {}

  Token() : Token(SourceSpan(), SourceSpan(), Token::Kind::kNotAToken, Token::Subkind::kNone) {}

  static const char* Name(KindAndSubkind kind_and_subkind) {
    switch (kind_and_subkind.combined()) {
#define TOKEN(Name)          \
  case Token::Kind::k##Name: \
    return #Name;
#include "tools/fidl/fidlc/include/fidl/token_definitions.inc"
#undef TOKEN
#define TOKEN_SUBKIND(Name, Spelling)                                                       \
  case Token::KindAndSubkind(Token::Kind::kIdentifier, Token::Subkind::k##Name).combined(): \
    return #Spelling;
#include "tools/fidl/fidlc/include/fidl/token_definitions.inc"
#undef TOKEN_SUBKIND
      default:
        return "<unknown token>";
    }
  }

  std::string_view data() const { return span_.data(); }
  const SourceSpan& span() const { return span_; }
  void set_previous_end(SourceSpan span) { previous_end_ = span; }
  SourceSpan previous_end() const { return previous_end_; }
  Kind kind() const { return kind_and_subkind_.kind(); }
  Subkind subkind() const { return kind_and_subkind_.subkind(); }
  KindAndSubkind kind_and_subkind() const { return kind_and_subkind_; }

  constexpr bool operator==(const Token& rhs) const { return span_ == rhs.span_; }
  constexpr bool operator!=(const Token& rhs) const { return !(*this == rhs); }
  constexpr bool operator<(const Token& rhs) const { return span_ < rhs.span_; }

 private:
  // The end of the previous token.  Everything between this and span_ is
  // somehow uninteresting to the parser (whitespace, comments, discarded
  // braces, etc).
  SourceSpan previous_end_;
  SourceSpan span_;
  KindAndSubkind kind_and_subkind_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TOKEN_H_

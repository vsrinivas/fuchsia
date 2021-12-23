// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_STRING_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_STRING_H_

#include <optional>
#include <string>
#include <string_view>

#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/expr_token_type.h"

namespace zxdb {

// Parsing information for string and character literals.
struct StringOrCharLiteralBegin {
  ExprLanguage language = ExprLanguage::kC;

  // On success this will be either kStringLiteral or kCharLiteral.
  ExprTokenType token_type = ExprTokenType::kInvalid;

  // Set when this is a raw string. If we need to support Rust byte strings this may become an
  // enum. Only set when token_type == ExprTokenType::kStringLiteral.
  bool is_raw = false;

  // For raw strings, this is the marker for the end of the string. For rust this will be some
  // nonzero number of '#' characters. For C++, this will be the sequence between the opening quote
  // and the opening paren (which will often be empty).
  std::string_view raw_marker;

  // Index into the input string of the first character of the string input (this will be the
  // opening quote or "R" character).
  size_t string_begin = 0;

  // Index into the input string of the first character after the prefix (this will be the first
  // thing in the string or character itself).
  size_t contents_begin = 0;
};

// Returns true if the string begins a Rust lifetime literal. This requires some disambiguation
// because 'a is a lifetime but 'a' is a character.
bool DoesBeginRustLifetime(std::string_view input);

// Returns true if the given location in the input string starts with a string or character literal.
// In Rust, this will return nullopt for lifetime annotations like 'foo, so the caller can assume
// anything starting with ' that this returns false for is a lifetime.
//
// Returns a StringOrCharLiteralBegin if the current location starts a string or character. This can
// be passed into ParseString if so.
//
// When parsing a raw string prefix, we may encounter a state where we know it should be a string
// prefix but it's malformed. Currently we don't report the error and we say it's not a string.
// The return value could be converted to a tri-state (not a string, string, error) if needed.
std::optional<StringOrCharLiteralBegin> DoesBeginStringOrCharLiteral(ExprLanguage lang,
                                                                     std::string_view input,
                                                                     size_t cur);

// Parses a string or character literal starting at index |*cur| inside |input|. On error, the
// |*error_location| will be set to the byte that goes along with the error. The info should have
// been computed by DoesBeginStringLiteral().
ErrOr<std::string> ParseStringOrCharLiteral(std::string_view input,
                                            const StringOrCharLiteralBegin& info, size_t* cur,
                                            size_t* error_location);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_STRING_H_

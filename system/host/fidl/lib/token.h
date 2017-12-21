// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "string_view.h"

namespace fidl {

// A Token represents a typed view into a source buffer. That is, it
// has a TokenKind, and it has a buffer representing the data
// corresponding to the token. No processing is done on the data:
// string or numeric literals aren't further parsed, identifiers
// uniqued, and so on.
class Token {
public:
    enum Kind : uint8_t {
#define TOKEN(Name) Name,
#include "token_definitions.inc"
    };

    Token(StringView data, uint32_t offset, Kind kind)
        : data_(data), offset_(offset), kind_(kind) {}

    Token() : Token(StringView(), 0u, Token::Kind::NotAToken) {}

    static const char* Name(Kind kind) {
        switch (kind) {
#define TOKEN(Name)                                                                                \
    case fidl::Token::Kind::Name:                                                                  \
        return #Name;
#include "token_definitions.inc"
        }
    }

    constexpr StringView data() const { return data_; }
    constexpr uint32_t offset() const { return offset_; }
    constexpr Kind kind() const { return kind_; }

private:
    StringView data_;
    uint32_t offset_;
    Kind kind_;
};

} // namespace fidl

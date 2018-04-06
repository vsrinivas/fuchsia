// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_

#include <stdint.h>

#include "source_location.h"
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
#include "fidl/token_definitions.inc"
    };

    Token(SourceLocation location, Kind kind) : location_(location), kind_(kind) {}

    Token() : Token(SourceLocation(), Token::Kind::NotAToken) {}

    static const char* Name(Kind kind) {
        switch (kind) {
#define TOKEN(Name)                                                                                \
    case fidl::Token::Kind::Name:                                                                  \
        return #Name;
#include "fidl/token_definitions.inc"
        }
    }

    StringView data() const { return location_.data(); }
    SourceLocation location() const { return location_; }
    Kind kind() const { return kind_; }

private:
    SourceLocation location_;
    Kind kind_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_

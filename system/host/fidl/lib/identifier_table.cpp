// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "identifier_table.h"

namespace fidl {

IdentifierTable::IdentifierTable() {
    keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::Name},
#include "token_definitions.h"
    };
}

Token IdentifierTable::MakeIdentifier(StringView source_data, uint32_t offset,
                                      bool escaped_identifier) const {
    auto kind = Token::Kind::Identifier;
    if (!escaped_identifier) {
        auto lookup = keyword_table_.find(source_data);
        if (lookup != keyword_table_.end())
            kind = lookup->second;
    }
    return Token(source_data, offset, kind);
}

} // namespace fidl

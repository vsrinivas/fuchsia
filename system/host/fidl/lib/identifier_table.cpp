// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/identifier_table.h"

#include "fidl/source_location.h"

namespace fidl {

IdentifierTable::IdentifierTable() {
    keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::k##Name},
#include "fidl/token_definitions.inc"
    };
}

Token IdentifierTable::MakeIdentifier(SourceLocation previous_end, StringView source_data, const SourceFile& source_file,
                                      bool escaped_identifier) const {
    auto kind = Token::Kind::kIdentifier;
    if (!escaped_identifier) {
        auto lookup = keyword_table_.find(source_data);
        if (lookup != keyword_table_.end())
            kind = lookup->second;
    }
    return Token(previous_end, SourceLocation(source_data, source_file), kind);
}

} // namespace fidl

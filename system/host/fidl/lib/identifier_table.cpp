// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/identifier_table.h"

#include "fidl/source_location.h"

namespace fidl {

IdentifierTable::IdentifierTable() {
    keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::Subkind::k##Name},
#include "fidl/token_definitions.inc"
#undef KEYWORD
    };
}

Token IdentifierTable::MakeIdentifier(SourceLocation previous_end, StringView source_data, const SourceFile& source_file) const {
    auto subkind = Token::Subkind::kNone;
    auto lookup = keyword_table_.find(source_data);
    if (lookup != keyword_table_.end())
        subkind = lookup->second;
    return Token(previous_end, SourceLocation(source_data, source_file), Token::Kind::kIdentifier, subkind);
}

} // namespace fidl

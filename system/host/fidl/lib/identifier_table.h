// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>

#include "string_view.h"
#include "token.h"

namespace fidl {

class IdentifierTable {
public:
    IdentifierTable();

    Token MakeIdentifier(StringView source_data, uint32_t offset, bool escaped_identifier) const;

private:
    std::map<StringView, Token::Kind> keyword_table_;
};

} // namespace fidl

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_IDENTIFIER_TABLE_H_
#define ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_IDENTIFIER_TABLE_H_

#include <stdint.h>

#include <map>

#include "banjo/source_file.h"
#include "string_view.h"
#include "token.h"
#include "types.h"

namespace banjo {

class IdentifierTable {
public:
    IdentifierTable();

    Token MakeIdentifier(SourceLocation previous_end,
                         StringView source_data, const SourceFile& source_file,
                         bool escaped_identifier) const;

private:
    std::map<StringView, Token::Subkind> keyword_table_;
};

} // namespace banjo

#endif // ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_IDENTIFIER_TABLE_H_

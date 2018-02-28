// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_IDENTIFIER_TABLE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_IDENTIFIER_TABLE_H_

#include <stdint.h>

#include <map>

#include "fidl/source_file.h"
#include "string_view.h"
#include "token.h"
#include "types.h"

namespace fidl {

class IdentifierTable {
public:
    IdentifierTable();

    Token MakeIdentifier(StringView source_data, const SourceFile& source_file,
                         bool escaped_identifier) const;

private:
    std::map<StringView, Token::Kind> keyword_table_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_IDENTIFIER_TABLE_H_

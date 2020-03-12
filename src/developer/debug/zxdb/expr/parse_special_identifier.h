// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_SPECIAL_IDENTIFIER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_SPECIAL_IDENTIFIER_H_

#include <string>
#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/identifier_base.h"

namespace zxdb {

// Parses a special identifier of "$special_name(contents)" or "$special_name". It starts at index
// |*cur| inside |input|. On error, |*error_location| will be set to the byte that goes along with
// the error.
Err ParseSpecialIdentifier(std::string_view input, size_t* cur, SpecialIdentifier* special,
                           std::string* contents, size_t* error_location);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSE_SPECIAL_IDENTIFIER_H_

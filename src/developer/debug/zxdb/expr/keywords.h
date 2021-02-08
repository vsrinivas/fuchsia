// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_KEYWORDS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_KEYWORDS_H_

#include <set>
#include <string>

#include "src/developer/debug/zxdb/expr/expr_language.h"

namespace zxdb {

// Returns the set of all keywords for the given language. If "permissive" is set, the set will
// include names that aren't strictly built-in but are commonly thought of as built-in, like
// "int32_t" in C.
const std::set<std::string>& AllKeywordsForLanguage(ExprLanguage language, bool permissive);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_KEYWORDS_H_

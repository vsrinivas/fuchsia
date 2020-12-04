// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_OPERATOR_KEYWORD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_OPERATOR_KEYWORD_H_

#include <vector>

#include "src/developer/debug/zxdb/expr/expr_token.h"

namespace zxdb {

struct OperatorKeywordResult {
  bool success = false;

  // On success, this will be something like "operator++", because the input might have spaces after
  // the "operator" keyword.
  std::string canonical_name;

  // Token immediately following the last consumed token.
  size_t end_token = 0;
};

// This extracts the built-in operator names like "operator++" and "operator()". It does not
// parse type conversion function names like "operator bool". The ExprParser will handle that.
//
// The token corresponding to the "operator" keyword itself is passed in as an argument. The
// end_token in the result will indicate the tokens consumed.
OperatorKeywordResult ParseOperatorKeyword(const std::vector<ExprToken>& tokens,
                                           size_t keyword_token);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_OPERATOR_KEYWORD_H_

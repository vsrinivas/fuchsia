// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr.h"

#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_parser.h"
#include "garnet/bin/zxdb/expr/expr_tokenizer.h"

namespace zxdb {

void EvalExpression(const std::string& input,
                    fxl::RefPtr<ExprEvalContext> context,
                    std::function<void(const Err& err, ExprValue value)> cb) {
  ExprTokenizer tokenizer(input);
  if (!tokenizer.Tokenize()) {
    cb(tokenizer.err(), ExprValue());
    return;
  }

  ExprParser parser(tokenizer.TakeTokens());
  auto node = parser.Parse();
  if (parser.err().has_error()) {
    // Add context information since we have the original input string (the
    // parser doesn't have this).
    ExprToken error_token = parser.error_token();
    if (error_token.type() != ExprToken::Type::kInvalid) {
      Err context_err(
          parser.err().type(),
          parser.err().msg() + "\n" +
              ExprTokenizer::GetErrorContext(input, error_token.byte_offset()));
      cb(context_err, ExprValue());
    } else {
      cb(parser.err(), ExprValue());
    }
    return;
  }

  node->Eval(context, std::move(cb));
}

}  // namespace zxdb

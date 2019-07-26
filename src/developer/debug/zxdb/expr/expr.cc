// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr.h"

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

class MultiEvalTracking {
 public:
  using OutputVector = std::vector<std::pair<Err, ExprValue>>;
  using Completion = fit::callback<void(OutputVector)>;

  MultiEvalTracking(size_t count, Completion cb) : remaining_(count), completion_(std::move(cb)) {
    data_.resize(count);
  }

  void SetResult(size_t index, const Err& err, ExprValue val) {
    FXL_DCHECK(index < data_.size());
    FXL_DCHECK(remaining_ > 0);

    // Nothing should be set on this slot yet.
    FXL_DCHECK(!data_[index].first.has_error());
    FXL_DCHECK(data_[index].second == ExprValue());

    data_[index].first = err;
    data_[index].second = std::move(val);

    remaining_--;
    if (remaining_ == 0)
      completion_(std::move(data_));
  }

 private:
  size_t remaining_;  // # callbacks remaining before completion.
  OutputVector data_;
  Completion completion_;
};

}  // namespace

void EvalExpression(const std::string& input, fxl::RefPtr<EvalContext> context,
                    bool follow_references,
                    fit::callback<void(const Err& err, ExprValue value)> cb) {
  ExprTokenizer tokenizer(input, context->GetLanguage());
  if (!tokenizer.Tokenize()) {
    cb(tokenizer.err(), ExprValue());
    return;
  }

  ExprParser parser(tokenizer.TakeTokens(), context->GetSymbolNameLookupCallback());
  auto node = parser.Parse();
  if (parser.err().has_error()) {
    // Add context information since we have the original input string (the
    // parser doesn't have this).
    ExprToken error_token = parser.error_token();
    if (error_token.type() != ExprTokenType::kInvalid) {
      Err context_err(parser.err().type(),
                      parser.err().msg() + "\n" +
                          ExprTokenizer::GetErrorContext(input, error_token.byte_offset()));
      cb(context_err, ExprValue());
    } else {
      cb(parser.err(), ExprValue());
    }
    return;
  }

  if (follow_references)
    node->Eval(context, std::move(cb));
  else
    node->EvalFollowReferences(context, std::move(cb));
}

void EvalExpressions(const std::vector<std::string>& inputs, fxl::RefPtr<EvalContext> context,
                     bool follow_references,
                     fit::callback<void(std::vector<std::pair<Err, ExprValue>>)> cb) {
  FXL_DCHECK(!inputs.empty());

  auto tracking = std::make_shared<MultiEvalTracking>(inputs.size(), std::move(cb));
  for (size_t i = 0; i < inputs.size(); i++) {
    EvalExpression(inputs[i], context, follow_references,
                   [tracking, i](const Err& err, ExprValue value) {
                     tracking->SetResult(i, err, std::move(value));
                   });
  }
}

}  // namespace zxdb

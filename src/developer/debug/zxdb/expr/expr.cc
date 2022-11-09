// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/developer/debug/zxdb/expr/vm_exec.h"
#include "src/developer/debug/zxdb/expr/vm_stream.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"

namespace zxdb {

namespace {

class MultiEvalTracking {
 public:
  using OutputVector = std::vector<ErrOrValue>;
  using Completion = fit::callback<void(OutputVector)>;

  MultiEvalTracking(size_t count, Completion cb) : remaining_(count), completion_(std::move(cb)) {
    data_.resize(count, ErrOrValue(ExprValue()));
  }

  void SetResult(size_t index, ErrOrValue value) {
    FX_DCHECK(index < data_.size());
    FX_DCHECK(remaining_ > 0);

    // Nothing should be set on this slot yet.
    FX_DCHECK(!data_[index].has_error());
    FX_DCHECK(data_[index].value() == ExprValue());

    data_[index] = std::move(value);

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

void EvalExpression(const std::string& input, const fxl::RefPtr<EvalContext>& context,
                    bool follow_references, EvalCallback cb) {
  ExprTokenizer tokenizer(input, context->GetLanguage());
  if (!tokenizer.Tokenize())
    return cb(tokenizer.err());

  ExprParser parser(tokenizer.TakeTokens(), tokenizer.language(), context);
  auto node = parser.ParseStandaloneExpression();
  if (parser.err().has_error()) {
    // Add context information since we have the original input string (the
    // parser doesn't have this).
    ExprToken error_token = parser.error_token();
    if (error_token.type() != ExprTokenType::kInvalid) {
      Err context_err(parser.err().type(),
                      parser.err().msg() + "\n" +
                          ExprTokenizer::GetErrorContext(input, error_token.byte_offset()));
      cb(context_err);
    } else {
      cb(parser.err());
    }
    return;
  }

  VmStream stream;
  node->EmitBytecode(stream);
  if (follow_references)
    stream.push_back(VmOp::MakeExpandRef());

  VmExec(context, std::move(stream), std::move(cb));
}

void EvalExpressions(const std::vector<std::string>& inputs,
                     const fxl::RefPtr<EvalContext>& context, bool follow_references,
                     fit::callback<void(std::vector<ErrOrValue>)> cb) {
  FX_DCHECK(!inputs.empty());

  auto tracking = std::make_shared<MultiEvalTracking>(inputs.size(), std::move(cb));
  for (size_t i = 0; i < inputs.size(); i++) {
    EvalExpression(inputs[i], context, follow_references,
                   [tracking, i](ErrOrValue value) { tracking->SetResult(i, std::move(value)); });
  }
}

// TODO(bug 44074) support non-pointer values and take their address implicitly.
Err ValueToAddressAndSize(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& value,
                          uint64_t* address, std::optional<uint32_t>* size) {
  fxl::RefPtr<Type> concrete_type = eval_context->GetConcreteType(value.type());
  if (concrete_type->As<Collection>()) {
    // Don't allow structs and classes that are <= 64 bits to be converted
    // to addresses.
    return Err("Can't convert '%s' to an address.", concrete_type->GetFullName().c_str());
  }

  // See if there's an intrinsic size to the object being pointed to. This is true for pointers.
  // References should have been followed and stripped before here.
  if (auto modified = concrete_type->As<ModifiedType>();
      modified && modified->tag() == DwarfTag::kPointerType) {
    if (auto modified_type = modified->modified().Get()->As<Type>())
      *size = modified_type->byte_size();
  }

  // Convert anything else <= 64 bits to a number.
  return value.PromoteTo64(address);
}

}  // namespace zxdb

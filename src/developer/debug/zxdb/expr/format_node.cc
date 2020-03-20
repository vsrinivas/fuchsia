// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format_node.h"

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

FormatNode::FormatNode(const std::string& name) : name_(name), weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, ExprValue value)
    : source_(kValue),
      state_(kHasValue),
      name_(name),
      value_(std::move(value)),
      weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, ErrOrValue err_or_value)
    : source_(kValue),
      state_(err_or_value.has_error() ? kDescribed : kHasValue),
      name_(name),
      value_(err_or_value.take_value_or_empty()),
      err_(err_or_value.err_or_empty()),
      weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, Err err)
    : source_(kValue), state_(kDescribed), name_(name), err_(std::move(err)), weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, const std::string& expression)
    : source_(kExpression),
      state_(kUnevaluated),
      name_(name),
      expression_(expression),
      weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, GetProgramaticValue get_value)
    : source_(kProgramatic),
      state_(kUnevaluated),
      name_(name),
      get_programatic_value_(std::move(get_value)),
      weak_factory_(this) {
  FXL_DCHECK(get_programatic_value_);  // Caller must specify nonempty func.
}

FormatNode::FormatNode(GroupTag)
    : source_(kValue),  // Don't compute a value, there is none.
      state_(kDescribed),
      description_kind_(kGroup),
      weak_factory_(this) {}

FormatNode::~FormatNode() = default;

fxl::WeakPtr<FormatNode> FormatNode::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void FormatNode::FillProgramaticValue(const fxl::RefPtr<EvalContext>& context,
                                      fit::deferred_callback cb) {
  FXL_DCHECK(source() == kProgramatic);
  FXL_DCHECK(get_programatic_value_);
  get_programatic_value_(std::move(context), [weak_node = GetWeakPtr(), cb = std::move(cb)](
                                                 const Err& err, ExprValue value) {
    if (weak_node) {
      weak_node->state_ = kHasValue;
      if (err.has_error())
        weak_node->set_err(err);
      else
        weak_node->SetValue(std::move(value));
    }
  });
}

void FormatNode::SetValue(ExprValue v) {
  value_ = std::move(v);
  set_state(kHasValue);
}

void FormatNode::SetDescribedError(const Err& e) {
  set_err(e);
  set_state(kDescribed);
}

}  // namespace zxdb

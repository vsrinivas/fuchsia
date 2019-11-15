// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_rust_tuple.h"

#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

void PrettyRustTuple::Format(FormatNode* node, const FormatOptions& options,
                             const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  auto collection = node->value().type()->AsCollection();

  if (collection->GetSpecialType() == Collection::kRustTupleStruct) {
    node->set_description_kind(FormatNode::kRustTupleStruct);
  } else {
    node->set_description_kind(FormatNode::kRustTuple);
  }

  // Rust tuple (and tuple struct) symbols have the tuple members encoded as "__0", "__1", etc.
  for (const auto& lazy_member : collection->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    // Convert the names to indices "__0" -> 0.
    auto name = member->GetAssignedName();
    if (name.size() > 2 && name[0] == '_' && name[1] == '_')
      name.erase(name.begin(), name.begin() + 2);

    // In the error case, still append a child so that the child can have the error associated
    // with it.
    node->children().push_back(std::make_unique<FormatNode>(
        name, ResolveNonstaticMember(context, node->value(), FoundMember(member))));
  }
}

PrettyRustTuple::EvalFunction PrettyRustTuple::GetMember(const std::string& getter_name) const {
  if (getter_name.empty()) {
    return EvalFunction();
  }

  for (const auto& ch : getter_name) {
    if (ch < '0' || ch > '9') {
      return EvalFunction();
    }
  }

  return [getter_name](const fxl::RefPtr<EvalContext>& context, const ExprValue& object_value,
                       EvalCallback cb) {
    // Rust tuple members are named __0, __1, etc.
    EvalExpressionOn(context, object_value, "__" + getter_name, std::move(cb));
  };
}

void PrettyRustZirconStatus::Format(FormatNode* node, const FormatOptions& options,
                                    const fxl::RefPtr<EvalContext>& context,
                                    fit::deferred_callback cb) {
  auto collection = node->value().type()->AsCollection();
  if (!collection || collection->GetSpecialType() != Collection::kRustTupleStruct) {
    return;
  }

  node->set_description_kind(FormatNode::kRustTupleStruct);

  const auto& members = collection->data_members();
  if (members.size() != 1) {
    return;
  }

  if (const DataMember* member = members[0].Get()->AsDataMember()) {
    auto child = std::make_unique<FormatNode>(
        "0", ResolveNonstaticMember(context, node->value(), FoundMember(member)));

    // The status formatter can return immediately, meaning this node gets printed before this
    // function is done, so we have to share the return callback, not just pass it along.
    auto shared_cb = std::make_shared<fit::deferred_callback>(std::move(cb));

    PrettyZxStatusT status_formatter;
    status_formatter.Format(child.get(), options, context,
                            fit::defer_callback([shared_cb]() mutable {}));

    child->set_state(FormatNode::kDescribed);
    child->set_source(FormatNode::kDescription);

    node->children().push_back(std::move(child));
  }
}

}  // namespace zxdb

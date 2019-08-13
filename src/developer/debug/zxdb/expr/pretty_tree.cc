// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_tree.h"

#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Fills the given node for a set/map iterator. The "value" is the referenced value. The container
// type will be either "map" or "set" to make the description.
void FillTreeIteratorNode(const char* container_type, FormatNode* node,
                          fxl::RefPtr<EvalContext> context, const Err& err, ExprValue value) {
  if (err.has_error())
    return node->SetDescribedError(err);

  // Declare it as a pointer with the value as the pointed-to thing.
  node->set_description_kind(FormatNode::kPointer);

  // There isn't a good address to show since the actual pointer is to the tree node and showing
  // the node address in the description looks misleading. But some generic text.
  node->set_description(std::string(container_type) + "::iterator");

  // Make the dereference child node the value.
  auto deref_node = std::make_unique<FormatNode>("*", std::move(value));
  deref_node->set_child_kind(FormatNode::kPointerExpansion);
  node->children().push_back(std::move(deref_node));
}

}  // namespace

// PrettyTreeIterator ------------------------------------------------------------------------------

void PrettyTreeIterator::Format(FormatNode* node, const FormatOptions& options,
                                fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  GetIteratorValue(context, node->value(),
                   [context, weak_node = node->GetWeakPtr(), cb = std::move(cb)](const Err& err,
                                                                                 ExprValue value) {
                     if (weak_node) {
                       FillTreeIteratorNode("std::set", weak_node.get(), std::move(context), err,
                                            std::move(value));
                     }
                   });
}

PrettyTreeIterator::EvalFunction PrettyTreeIterator::GetDereferencer() const {
  return [](fxl::RefPtr<EvalContext> context, const ExprValue& iter,
            fit::callback<void(const Err&, ExprValue)> cb) {
    GetIteratorValue(std::move(context), iter, std::move(cb));
  };
}

void PrettyTreeIterator::GetIteratorValue(fxl::RefPtr<EvalContext> context, const ExprValue& iter,
                                          fit::callback<void(const Err&, ExprValue)> cb) {
  // Evaluate "static_cast<ITER_TYPE::__node_pointer>(iter.__ptr_)->__value_"
  //
  // Unfortunately, there is no way with the implementation to know when an iterator points to
  // "end" other than it's values look fishy.
  //
  // It would be nice to express this solely in terms of expressions if we can figure out how to
  // express "TREE_TYPE" in the above expression.
  EvalExpressionOn(
      context, iter,
      "reinterpret_cast<" + iter.type()->GetFullName() + "::__node_pointer>(__ptr_)->__value_",
      std::move(cb));
}

// PrettyMapIterator -------------------------------------------------------------------------------

void PrettyMapIterator::Format(FormatNode* node, const FormatOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  GetIteratorValue(context, node->value(),
                   [context, weak_node = node->GetWeakPtr(), cb = std::move(cb)](const Err& err,
                                                                                 ExprValue value) {
                     if (weak_node) {
                       FillTreeIteratorNode("std::map", weak_node.get(), std::move(context), err,
                                            std::move(value));
                     }
                   });
}

PrettyMapIterator::EvalFunction PrettyMapIterator::GetDereferencer() const {
  return [](fxl::RefPtr<EvalContext> context, const ExprValue& iter,
            fit::callback<void(const Err&, ExprValue)> cb) {
    GetIteratorValue(std::move(context), iter, std::move(cb));
  };
}

void PrettyMapIterator::GetIteratorValue(fxl::RefPtr<EvalContext> context, const ExprValue& iter,
                                         fit::callback<void(const Err&, ExprValue)> cb) {
  // Evaluate "static_cast<ITER_TYPE::__node_pointer>(iter.__i_.__ptr_)->__value_.__cc"
  // Where ITER_TYPE is actually the type of "iter.__i_".
  ErrOrValue i_value = ResolveMember(context, iter, ParsedIdentifier("__i_"));
  if (i_value.has_error())
    return cb(i_value.err(), ExprValue());

  // See PrettyTreeIterator above.
  EvalExpressionOn(context, i_value.value(),
                   "reinterpret_cast<" + i_value.value().type()->GetFullName() +
                       "::__node_pointer>(__ptr_)->__value_.__cc",
                   std::move(cb));
}

// PrettyTree --------------------------------------------------------------------------------------

// C++ expression that resolves to a set/map size.
#define TREE_SIZE_EXPRESSION "__tree_.__pair3_.__value_"

PrettyTree::PrettyTree(const std::string& container_name)
    : PrettyType({{"size", TREE_SIZE_EXPRESSION}, {"empty", TREE_SIZE_EXPRESSION " == 0"}}),
      container_name_(container_name) {}

void PrettyTree::Format(FormatNode* node, const FormatOptions& options,
                        fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  // Actually getting the set contents in our C++ code with our current asynchronous API is
  // prohibitive. When we have a way that walking the tree can be expressed in a synchronous
  // fashion (either by a scripting language or fancier expressions) we can add this ability.
  //
  // For now, just show the size as the description.
  EvalExpressionOn(context, node->value(), TREE_SIZE_EXPRESSION,
                   [container_name = container_name_, weak_node = node->GetWeakPtr(),
                    cb = std::move(cb)](const Err& err, ExprValue size_value) mutable {
                     if (!weak_node)
                       return;
                     if (err.has_error())
                       return weak_node->SetDescribedError(err);

                     uint64_t size = 0;
                     if (Err e = size_value.PromoteTo64(&size); e.has_error())
                       return weak_node->SetDescribedError(err);

                     weak_node->set_description(
                         fxl::StringPrintf("%s{size = %" PRIu64 "}", container_name.c_str(), size));
                   });
}

}  // namespace zxdb

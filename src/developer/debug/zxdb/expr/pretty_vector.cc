// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_vector.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/template_parameter.h"

namespace zxdb {

namespace {

// Extracts the contained and allocator types for a template. Returns true on success.
bool ExtractVectorTypes(const Type* vector, fxl::RefPtr<Type>* contained,
                        fxl::RefPtr<Type>* allocator) {
  auto coll = vector->AsCollection();
  if (!coll)
    return false;

  if (coll->template_params().size() != 2)
    return false;

  const TemplateParameter* contained_param =
      coll->template_params()[0].Get()->AsTemplateParameter();
  const TemplateParameter* allocator_param =
      coll->template_params()[1].Get()->AsTemplateParameter();
  if (!contained_param || !allocator_param)
    return false;

  *contained = RefPtrTo(contained_param->type().Get()->AsType());
  *allocator = RefPtrTo(allocator_param->type().Get()->AsType());
  return *contained && *allocator;
}

// Returns a nice type description for a vector type if possible.
//
// The full description with the default allocator will look like:
//   std::__2::vector<TYPE, std::__2::allocator<TYPE>>
// In this case we want to rename the type to
//   std::vector<TYPE>
// If the allocator is non-default, it should be
//   std::vector<TYPE, ALLOCATOR>
// If anything is unexpected, returns the full type name.
std::string DescribeStdVectorType(const Type* contained, const Type* allocator) {
  // TODO(brettw) this comparison of the allocator could be more robust. Maybe the user passed an
  // allocator of a different type?
  if (StringBeginsWith(allocator->GetFullName(), "std::__2::allocator<"))
    return "std::vector<" + contained->GetFullName() + ">";  // Default allocator.

  return "std::vector<" + contained->GetFullName() + ", " + allocator->GetFullName() + ">";
}

}  // namespace

// C++ std::vector ---------------------------------------------------------------------------------

// For a non-bool vector:
//   __begin_ is the return value of the begin() function.
//   __end_ is the return value of the end() function.
//   __end_cap_ is like an iterator to the end of the capacity().
//
// This additionally tries to clean up the type name to remove the allocator when it's the default
// one. This is nice but difficult to express. With the final pretty-printer design we should
// evaluate whether this is worthwhile given the benefit.
//
// In a higher-level pretty-printer expression this could be something along the lines of:
//   array(__begin_, __end_ - __begin_)
void PrettyStdVector::Format(FormatNode* node, const FormatOptions& options,
                             fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  auto type = context->GetConcreteType(node->value().type());

  fxl::RefPtr<Type> contained;
  fxl::RefPtr<Type> allocator;
  if (!ExtractVectorTypes(type.get(), &contained, &allocator)) {
    node->SetDescribedError(Err("Unexpected std::vector format."));
    return;
  }
  fxl::RefPtr<Type> concrete_contained = context->GetConcreteType(contained.get());

  node->set_type(DescribeStdVectorType(contained.get(), allocator.get()));

  ExprValue begin;
  if (Err err = ExtractMember(context, node->value(), {"__begin_"}, &begin); err.has_error())
    return node->SetDescribedError(err);

  ExprValue end;
  if (Err err = ExtractMember(context, node->value(), {"__end_"}, &end); err.has_error())
    return node->SetDescribedError(err);

  // Ideally we could do "__end_ - __begin_" in the expression language which could compute
  // the correct size. For now, assume the concrete type gives us the correct size and extract the
  // pointers manually.
  uint64_t begin_value, end_value;
  if (Err err = begin.PromoteTo64(&begin_value); err.has_error())
    return node->SetDescribedError(err);
  if (Err err = end.PromoteTo64(&end_value); err.has_error())
    return node->SetDescribedError(err);

  auto type_size = contained->byte_size();
  if (type_size == 0)
    return node->SetDescribedError(Err("Bad type information for std::vector."));

  int item_count = std::min((end_value - begin_value) / type_size,
                            static_cast<uint64_t>(std::numeric_limits<int>::max()));

  FormatArrayNode(node, begin, item_count, options, context, std::move(cb));
}

}  // namespace zxdb

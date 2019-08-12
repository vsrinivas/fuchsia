// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// An EvalContext that shadows another one and injects all members of a given value into the
// current namespace. This allows pretty-printers to reference variables on the object being printed
// as if the code was in the context of that object.
//
// So for example, when pretty-printing the type:
//
//   struct Foo {
//     int bar;
//     char baz;
//   };
//
// The |value| passed in to the constructor would be the "Foo" instance. Expressions evaluated
// using this context can then refer to "bar" and "baz" without qualification.
class PrettyEvalContext : public EvalContext {
 public:
  PrettyEvalContext(fxl::RefPtr<EvalContext> impl, ExprValue value)
      : impl_(std::move(impl)), value_(std::move(value)) {}

  // EvalContext implementation. Everything except GetNamedValue() passes through to the impl_.
  ExprLanguage GetLanguage() const { return impl_->GetLanguage(); }
  void GetNamedValue(const ParsedIdentifier& name, ValueCallback cb) const;
  void GetVariableValue(fxl::RefPtr<Variable> variable, ValueCallback cb) const {
    return impl_->GetVariableValue(std::move(variable), std::move(cb));
  }
  fxl::RefPtr<Type> ResolveForwardDefinition(const Type* type) const {
    return impl_->ResolveForwardDefinition(type);
  }
  fxl::RefPtr<Type> GetConcreteType(const Type* type) const { return impl_->GetConcreteType(type); }
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() { return impl_->GetDataProvider(); }
  NameLookupCallback GetSymbolNameLookupCallback() { return impl_->GetSymbolNameLookupCallback(); }
  Location GetLocationForAddress(uint64_t address) const {
    return impl_->GetLocationForAddress(address);
  }
  const PrettyTypeManager& GetPrettyTypeManager() const { return impl_->GetPrettyTypeManager(); }

 private:
  fxl::RefPtr<EvalContext> impl_;
  ExprValue value_;
};

void PrettyEvalContext::GetNamedValue(const ParsedIdentifier& name, ValueCallback cb) const {
  // First try to resolve all names on the object given.
  if (ErrOrValue result = ResolveMember(impl_, value_, name); result.ok())
    return cb(result.err_or_empty(), fxl::RefPtr<Symbol>(), result.take_value_or_empty());

  // Fall back on regular name lookup.
  return impl_->GetNamedValue(name, std::move(cb));
}

// When doing multi-evaluation, we'll have a vector of values, any of which could have generated an
// error. This checks for errors and returns the first one.
Err UnionErrors(const std::vector<std::pair<Err, ExprValue>>& input) {
  for (const auto& cur : input) {
    if (cur.first.has_error())
      return cur.first;
  }
  return Err();
}

}  // namespace

PrettyType::PrettyType(std::initializer_list<std::pair<std::string, std::string>> getters) {
  for (const auto& cur : getters)
    AddGetterExpression(cur.first, cur.second);
}

void PrettyType::AddGetterExpression(const std::string& getter_name,
                                     const std::string& expression) {
  getters_[getter_name] = expression;
}

PrettyType::EvalFunction PrettyType::GetGetter(const std::string& getter_name) const {
  auto found = getters_.find(getter_name);
  if (found == getters_.end())
    return EvalFunction();
  return
      [expression = found->second](fxl::RefPtr<EvalContext> context, const ExprValue& object_value,
                                   fit::callback<void(const Err&, ExprValue)> cb) {
        EvalExpressionOn(context, object_value, expression, std::move(cb));
      };
}

void PrettyType::EvalExpressionOn(fxl::RefPtr<EvalContext> context, const ExprValue& object,
                                  const std::string& expression,
                                  fit::callback<void(const Err&, ExprValue result)> cb) {
  // Evaluates the expression in our magic wrapper context that promotes members to the active
  // context.
  EvalExpression(expression, fxl::MakeRefCounted<PrettyEvalContext>(context, object), true,
                 std::move(cb));
}

// static
Err PrettyType::ExtractMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                              std::initializer_list<std::string> names, ExprValue* extracted) {
  ExprValue cur = value;
  for (const std::string& name : names) {
    ParsedIdentifier id(IdentifierQualification::kRelative, ParsedIdentifierComponent(name));
    ErrOrValue result = ResolveMember(context, cur, id);
    if (result.has_error())
      return result.err();

    cur = std::move(result.take_value());
  }
  *extracted = std::move(cur);
  return Err();
}

// static
Err PrettyType::Extract64BitMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                                   std::initializer_list<std::string> names, uint64_t* extracted) {
  ExprValue member;
  if (Err err = ExtractMember(context, value, names, &member); err.has_error())
    return err;
  return member.PromoteTo64(extracted);
}

void PrettyArray::Format(FormatNode* node, const FormatOptions& options,
                         fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  // Evaluate the expressions with this context to make the members in the current scope.
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpressions({ptr_expr_, size_expr_}, pretty_context, true,
                  [cb = std::move(cb), weak_node = node->GetWeakPtr(), options,
                   context](std::vector<std::pair<Err, ExprValue>> results) mutable {
                    FXL_DCHECK(results.size() == 2u);
                    if (!weak_node)
                      return;

                    if (Err e = UnionErrors(results); e.has_error())
                      return weak_node->SetDescribedError(e);

                    uint64_t len = 0;
                    if (Err err = results[1].second.PromoteTo64(&len); err.has_error())
                      return weak_node->SetDescribedError(err);

                    FormatArrayNode(weak_node.get(), results[0].second, len, options, context,
                                    std::move(cb));
                  });
}

PrettyArray::EvalArrayFunction PrettyArray::GetArrayAccess() const {
  // Since the PrettyArray is accessed by its pointer, we can just use the array access operator
  // combined with the pointer expression to produce an expression that references into the array.
  return [expression = ptr_expr_](fxl::RefPtr<EvalContext> context, const ExprValue& object_value,
                                  int64_t index, fit::callback<void(ErrOrValue)> cb) {
    EvalExpressionOn(context, object_value,
                     fxl::StringPrintf("(%s)[%" PRId64 "]", expression.c_str(), index),
                     ErrOrValue::ToPairCallback(std::move(cb)));
  };
}

void PrettyHeapString::Format(FormatNode* node, const FormatOptions& options,
                              fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  // Evaluate the expressions with this context to make the members in the current scope.
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpressions({ptr_expr_, size_expr_}, pretty_context, true,
                  [cb = std::move(cb), weak_node = node->GetWeakPtr(), options,
                   context](std::vector<std::pair<Err, ExprValue>> results) mutable {
                    FXL_DCHECK(results.size() == 2u);
                    if (!weak_node)
                      return;

                    if (Err err = UnionErrors(results); err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Pointed-to address.
                    uint64_t addr = 0;
                    if (Err err = results[0].second.PromoteTo64(&addr); err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Pointed-to type.
                    fxl::RefPtr<Type> char_type;
                    if (Err err = GetPointedToType(context, results[0].second.type(), &char_type);
                        err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Length.
                    uint64_t len = 0;
                    if (Err err = results[1].second.PromoteTo64(&len); err.has_error())
                      return weak_node->SetDescribedError(err);

                    FormatCharPointerNode(weak_node.get(), addr, char_type.get(), len, options,
                                          context, std::move(cb));
                  });
}

PrettyHeapString::EvalArrayFunction PrettyHeapString::GetArrayAccess() const {
  return [expression = ptr_expr_](fxl::RefPtr<EvalContext> context, const ExprValue& object_value,
                                  int64_t index, fit::callback<void(ErrOrValue)> cb) {
    EvalExpressionOn(context, object_value,
                     fxl::StringPrintf("(%s)[%" PRId64 "]", expression.c_str(), index),
                     ErrOrValue::ToPairCallback(std::move(cb)));
  };
}

void PrettyPointer::Format(FormatNode* node, const FormatOptions& options,
                           fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpression(expr_, pretty_context, true,
                 [cb = std::move(cb), weak_node = node->GetWeakPtr(), options](
                     const Err& err, ExprValue value) mutable {
                   if (!weak_node)
                     return;

                   if (err.has_error())
                     weak_node->SetDescribedError(err);
                   else
                     FormatPointerNode(weak_node.get(), value, options);
                 });
}

PrettyPointer::EvalFunction PrettyPointer::GetDereferencer() const {
  return [expr = expr_](fxl::RefPtr<EvalContext> context, const ExprValue& object_value,
                        fit::callback<void(const Err&, ExprValue)> cb) {
    // The value is from dereferencing the pointer value expression.
    EvalExpressionOn(context, object_value, "*(" + expr + ")", std::move(cb));
  };
}

}  // namespace zxdb

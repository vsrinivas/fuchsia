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
  ExprValue result;
  if (!ResolveMember(impl_, value_, name, &result).has_error())
    return cb(Err(), fxl::RefPtr<Symbol>(), std::move(result));

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
    ExprValue expr_value;
    if (Err err = ResolveMember(context, cur, id, &expr_value); err.has_error())
      return err;

    cur = std::move(expr_value);
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

}  // namespace zxdb

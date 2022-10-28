// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
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
  // Construct with fxl::MakeRefCounted().

  // EvalContext implementation. Everything except FindName()/GetNamedValue() passes through to the
  // impl_.
  ExprLanguage GetLanguage() const override { return impl_->GetLanguage(); }
  const std::shared_ptr<Abi>& GetAbi() const override { return impl_->GetAbi(); }
  void FindName(const FindNameOptions& options, const ParsedIdentifier& looking_for,
                std::vector<FoundName>* results) const override;
  FindNameContext GetFindNameContext() const override { return impl_->GetFindNameContext(); }
  void GetNamedValue(const ParsedIdentifier& name, EvalCallback cb) const override;
  void GetVariableValue(fxl::RefPtr<Value> variable, EvalCallback cb) const override {
    return impl_->GetVariableValue(std::move(variable), std::move(cb));
  }
  const ProcessSymbols* GetProcessSymbols() const override { return impl_->GetProcessSymbols(); }
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override { return impl_->GetDataProvider(); }
  Location GetLocationForAddress(uint64_t address) const override {
    return impl_->GetLocationForAddress(address);
  }
  const PrettyTypeManager& GetPrettyTypeManager() const override {
    return impl_->GetPrettyTypeManager();
  }
  VectorRegisterFormat GetVectorRegisterFormat() const override {
    return VectorRegisterFormat::kDouble;
  }
  bool ShouldPromoteToDerived() const override {
    // Pretty-printers should be coded such that they always handle the types given, so don't
    // promote to derived classes for them.
    return false;
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(PrettyEvalContext);
  FRIEND_MAKE_REF_COUNTED(PrettyEvalContext);

  PrettyEvalContext(fxl::RefPtr<EvalContext> impl, ExprValue value)
      : impl_(std::move(impl)), value_(std::move(value)) {}
  ~PrettyEvalContext() override = default;

  fxl::RefPtr<EvalContext> impl_;
  ExprValue value_;
};

void PrettyEvalContext::FindName(const FindNameOptions& options,
                                 const ParsedIdentifier& looking_for,
                                 std::vector<FoundName>* results) const {
  // TODO Hook this up to implicitly use the scope of the object being printed. Ideally it would
  // appear as the "this" member. Currently FindNameContext can't express this, it can only take
  // a CodeBlock where it looks for the "this" member. Ideally it could also take an object that
  // is automatically treated as "this".
  //
  // The parser will generally tolerate unknown words and treat them as identifiers which makes
  // parsing work. These cases will all end up in GetNamedValue() below when they're evaluated and
  // everything works.
  //
  // The thing that won't work is local types defined in the pretty-printed object. For C++ parsing
  // to work correctly, it needs to know which names are types, and this will force the
  // pretty-printer code to qualify all names, and means that types inside templates probably can't
  // be expressed at all. However, pretty-printers can always use "auto" which is sufficient for
  // most uses.
  return impl_->FindName(options, looking_for, results);
}

void PrettyEvalContext::GetNamedValue(const ParsedIdentifier& name, EvalCallback cb) const {
  // First try to resolve all names on the object given.
  ResolveMember(impl_, value_, name,
                [impl = impl_, name, cb = std::move(cb)](ErrOrValue value) mutable {
                  if (value.ok())
                    return cb(std::move(value));

                  // Fall back on regular name lookup.
                  impl->GetNamedValue(name, std::move(cb));
                });
}

// When doing multi-evaluation, we'll have a vector of values, any of which could have generated an
// error. This checks for errors and returns the first one.
Err UnionErrors(const std::vector<ErrOrValue>& input) {
  for (const auto& cur : input) {
    if (cur.has_error())
      return cur.err();
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
  return [expression = found->second](const fxl::RefPtr<EvalContext>& context,
                                      const ExprValue& object_value, EvalCallback cb) {
    EvalExpressionOn(context, object_value, expression, std::move(cb));
  };
}

void PrettyType::EvalExpressionOn(const fxl::RefPtr<EvalContext>& context, const ExprValue& object,
                                  const std::string& expression, EvalCallback cb) {
  // Evaluates the expression in our magic wrapper context that promotes members to the active
  // context.
  EvalExpression(expression, fxl::MakeRefCounted<PrettyEvalContext>(context, object), true,
                 std::move(cb));
}

void PrettyArray::Format(FormatNode* node, const FormatOptions& options,
                         const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  // Evaluate the expressions with this context to make the members in the current scope.
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpressions({ptr_expr_, size_expr_}, pretty_context, true,
                  [cb = std::move(cb), weak_node = node->GetWeakPtr(), options,
                   context](std::vector<ErrOrValue> results) mutable {
                    FX_DCHECK(results.size() == 2u);
                    if (!weak_node)
                      return;

                    if (Err e = UnionErrors(results); e.has_error())
                      return weak_node->SetDescribedError(e);

                    uint64_t len = 0;
                    if (Err err = results[1].value().PromoteTo64(&len); err.has_error())
                      return weak_node->SetDescribedError(err);

                    FormatArrayNode(weak_node.get(), results[0].value(), len, options, context,
                                    std::move(cb));
                  });
}

PrettyArray::EvalArrayFunction PrettyArray::GetArrayAccess() const {
  // Since the PrettyArray is accessed by its pointer, we can just use the array access operator
  // combined with the pointer expression to produce an expression that references into the array.
  return [expression = ptr_expr_](const fxl::RefPtr<EvalContext>& context,
                                  const ExprValue& object_value, int64_t index, EvalCallback cb) {
    EvalExpressionOn(context, object_value,
                     fxl::StringPrintf("(%s)[%" PRId64 "]", expression.c_str(), index),
                     std::move(cb));
  };
}

void PrettyHeapString::Format(FormatNode* node, const FormatOptions& options,
                              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  // Evaluate the expressions with this context to make the members in the current scope.
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpressions({ptr_expr_, size_expr_}, pretty_context, true,
                  [cb = std::move(cb), weak_node = node->GetWeakPtr(), options,
                   context](std::vector<ErrOrValue> results) mutable {
                    FX_DCHECK(results.size() == 2u);
                    if (!weak_node)
                      return;

                    if (Err err = UnionErrors(results); err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Pointed-to address.
                    uint64_t addr = 0;
                    if (Err err = results[0].value().PromoteTo64(&addr); err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Pointed-to type.
                    fxl::RefPtr<Type> char_type;
                    if (Err err = GetPointedToType(context, results[0].value().type(), &char_type);
                        err.has_error())
                      return weak_node->SetDescribedError(err);

                    // Length.
                    uint64_t len = 0;
                    if (Err err = results[1].value().PromoteTo64(&len); err.has_error())
                      return weak_node->SetDescribedError(err);

                    FormatCharPointerNode(weak_node.get(), addr, char_type.get(), len, options,
                                          context, std::move(cb));
                  });
}

PrettyHeapString::EvalArrayFunction PrettyHeapString::GetArrayAccess() const {
  return [expression = ptr_expr_](const fxl::RefPtr<EvalContext>& context,
                                  const ExprValue& object_value, int64_t index, EvalCallback cb) {
    EvalExpressionOn(context, object_value,
                     fxl::StringPrintf("(%s)[%" PRId64 "]", expression.c_str(), index),
                     std::move(cb));
  };
}

void PrettyPointer::Format(FormatNode* node, const FormatOptions& options,
                           const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value());

  EvalExpression(
      expr_, pretty_context, true,
      [cb = std::move(cb), weak_node = node->GetWeakPtr(), options](ErrOrValue value) mutable {
        if (!weak_node)
          return;

        if (value.has_error())
          weak_node->SetDescribedError(value.err());
        else
          FormatPointerNode(weak_node.get(), value.value(), options);
      });
}

PrettyPointer::EvalFunction PrettyPointer::GetDereferencer() const {
  return [expr = expr_](const fxl::RefPtr<EvalContext>& context, const ExprValue& object_value,
                        EvalCallback cb) {
    // The value is from dereferencing the pointer value expression.
    EvalExpressionOn(context, object_value, "*(" + expr + ")", std::move(cb));
  };
}

void PrettyOptional::Format(FormatNode* node, const FormatOptions& options,
                            const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  EvalOptional(
      context, node->value(), is_engaged_expr_, value_expr_,
      [simple_type_name = simple_type_name_, name_when_disengaged = name_when_disengaged_,
       cb = std::move(cb), weak_node = node->GetWeakPtr()](ErrOrValue value, bool is_empty) {
        if (!weak_node)
          return;

        if (is_empty)
          weak_node->set_description(name_when_disengaged);
        else if (value.has_error())
          weak_node->SetDescribedError(value.err());
        else
          FormatWrapper(weak_node.get(), simple_type_name, "(", ")", "", std::move(value));
      });
}

PrettyOptional::EvalFunction PrettyOptional::GetDereferencer() const {
  return
      [is_engaged_expr = is_engaged_expr_, value_expr = value_expr_,
       name_when_disengaged = name_when_disengaged_](
          const fxl::RefPtr<EvalContext>& context, const ExprValue& object_value, EvalCallback cb) {
        EvalOptional(
            context, object_value, is_engaged_expr, value_expr,
            [cb = std::move(cb), name_when_disengaged](ErrOrValue value, bool is_empty) mutable {
              if (is_empty)
                return cb(Err("Attempting to dereference a " + name_when_disengaged));
              cb(std::move(value));
            });
      };
}

// static
void PrettyOptional::EvalOptional(const fxl::RefPtr<EvalContext>& context, ExprValue object,
                                  const std::string& is_engaged_expr, const std::string& value_expr,
                                  fit::callback<void(ErrOrValue, bool is_empty)> cb) {
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, object);
  EvalExpression(
      is_engaged_expr, pretty_context, true,
      [pretty_context, cb = std::move(cb), value_expr](ErrOrValue is_engaged_value) mutable {
        if (is_engaged_value.has_error())
          return cb(is_engaged_value, false);

        uint64_t is_engaged = 0;
        if (Err e = is_engaged_value.value().PromoteTo64(&is_engaged); e.has_error())
          return cb(e, false);

        if (is_engaged) {
          // Valid, extract the value.
          EvalExpression(
              value_expr, pretty_context, true,
              [cb = std::move(cb)](ErrOrValue value) mutable { cb(std::move(value), false); });
        } else {
          // Not engaged, describe as "nullopt" or equivalent.
          cb(ExprValue(), true);
        }
      });
}

PrettyStruct::PrettyStruct(std::initializer_list<std::pair<std::string, std::string>> members)
    : PrettyType({}), members_(std::begin(members), std::end(members)) {}

void PrettyStruct::Format(FormatNode* node, const FormatOptions& options,
                          const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  node->set_description_kind(FormatNode::kCollection);

  // Generates a node for each "member_" that evaluates to the result of the corresponding expr.
  for (const auto& [name, expr] : members_) {
    auto child = std::make_unique<FormatNode>(
        name, [object = node->value(), expr = expr](
                  fxl::RefPtr<EvalContext> context,
                  fit::callback<void(const Err& err, ExprValue value)> cb) {
          EvalExpressionOn(context, object, expr, ErrOrValue::FromPairCallback(std::move(cb)));
        });
    node->children().push_back(std::move(child));
  }
}

void PrettyRecursiveVariant::Format(FormatNode* node, const FormatOptions& options,
                                    const fxl::RefPtr<EvalContext>& context,
                                    fit::deferred_callback cb) {
  auto eval_index_cb = [weak_node = node->GetWeakPtr(), simple_type_name = simple_type_name_,
                        base_expr = base_expr_, next_expr = next_expr_, value_expr = value_expr_,
                        no_value_string = no_value_string_, cb = std::move(cb)](ErrOrValue index) {
    if (!weak_node)
      return;
    if (index.has_error())
      return weak_node->SetDescribedError(index.err());

    // Index value.
    int64_t index_value = 0;
    if (Err err = index.value().PromoteTo64(&index_value); err.has_error())
      return weak_node->SetDescribedError(err);

    if (index_value < 0) {
      // This variant has no value.
      weak_node->set_description_kind(FormatNode::kOther);
      weak_node->set_description(no_value_string);
      return;
    }

    // Sanity check index to prevent crash on corrupt data.
    constexpr int64_t kMaxIndex = 16;
    if (index_value > kMaxIndex)
      return weak_node->SetDescribedError(Err("Variant index %" PRId64 " too large.", index_value));

    // This expression evaluates to the variant value (see header).
    std::string expr = base_expr;
    for (int64_t i = 0; i < index_value; i++) {
      if (!expr.empty())
        expr.push_back('.');
      expr.append(next_expr);
    }
    if (!value_expr.empty()) {
      expr.push_back('.');
      expr.append(value_expr);
    }

    FormatWrapper(weak_node.get(), simple_type_name, "(", ")", "",
                  [object = weak_node->value(), expr](
                      fxl::RefPtr<EvalContext> context,
                      fit::callback<void(const Err& err, ExprValue value)> cb) {
                    EvalExpressionOn(context, object, expr,
                                     ErrOrValue::FromPairCallback(std::move(cb)));
                  });
  };

  node->set_description_kind(FormatNode::kCollection);
  EvalExpressionOn(context, node->value(), index_expr_, std::move(eval_index_cb));
}

void PrettyWrappedValue::Format(FormatNode* node, const FormatOptions& options,
                                const fxl::RefPtr<EvalContext>& context,
                                fit::deferred_callback cb) {
  FormatWrapper(node, name_, open_bracket_, close_bracket_, "",
                [object = node->value(), expr = expression_](
                    fxl::RefPtr<EvalContext> context,
                    fit::callback<void(const Err& err, ExprValue value)> cb) {
                  EvalExpressionOn(context, object, expr,
                                   ErrOrValue::FromPairCallback(std::move(cb)));
                });
}

PrettyZxStatusT::PrettyZxStatusT() : PrettyType({}) {}

void PrettyZxStatusT::Format(FormatNode* node, const FormatOptions& options,
                             const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  FormatNumericNode(node, options);

  if (node->value().type()->byte_size() == sizeof(debug::zx_status_t)) {
    debug::zx_status_t int_val = node->value().GetAs<debug::zx_status_t>();
    node->set_description(node->description() +
                          fxl::StringPrintf(" (%s)", debug::ZxStatusToString(int_val)));
  }
}

}  // namespace zxdb

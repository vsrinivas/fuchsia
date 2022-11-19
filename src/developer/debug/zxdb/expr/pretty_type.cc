// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/common/leb.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// How to handle quotes for the name in PopulateName().
enum class NameQuotes { kStrip, kKeep };

// Populates the "key" or "name" of the given FormatNode with the given ExprValue. Since names are
// strings, we need a stringified version of the ExprValue.
//
// This is relatively simplistic. It just formats the value and takes the toplevel description of
// that node. If the key is some complicated struct, you probably can't handle that being formatted
// as the key in a list of array values anyway.
//
// It would be nice to give the pretty-printer more control over what the key is. Implementing
// something like StringPrintf might be nice, here showing a Golang-like "%v" for what our default
// description for any value would be:
//   StringPrintf("[%v]", key);
// We could also implement to_string() and use + to concatenate string literals:
//   "[" + $zxdb::to_string(key) + "]"
void PopulateName(const fxl::RefPtr<EvalContext>& eval_context, fxl::WeakPtr<FormatNode> weak_node,
                  const ExprValue& name, NameQuotes quotes, const FormatOptions& options,
                  EvalCallback cb) {
  // Create a format node to format the key.
  auto name_node = std::make_unique<FormatNode>(std::string(), name);

  // Asynchronously expand the key's FormatNode to get the string for it.
  FormatNode* name_node_ptr = name_node.get();
  FillFormatNodeDescription(
      name_node_ptr, options, eval_context,
      fit::defer_callback(
          [weak_node, quotes, name_node = std::move(name_node), cb = std::move(cb)]() mutable {
            if (weak_node) {
              const std::string& desc = name_node->description();
              if (quotes == NameQuotes::kStrip && desc.size() >= 2u && desc[0] == '"' &&
                  desc.back() == '"') {
                // Strip the quotes.
                weak_node->set_name(desc.substr(1, desc.size() - 2));
              } else {
                weak_node->set_name(desc);
              }
            }
            cb(ExprValue());  // Done, AppendKeyValueRow returns no value.
          }));
}

// Constructs an identifier in the $zxdb namespace with the given name.
ParsedIdentifier ZxdbNamespaced(const std::string& name) {
  ParsedIdentifier result((ParsedIdentifierComponent(SpecialIdentifier::kZxdb)));
  result.AppendComponent(ParsedIdentifierComponent(name));
  return result;
}

// Pretty-printing built-in functions --------------------------------------------------------------

// Implementation of the built-in pretty-printer function $zxdb::AppendKeyValueRow() and
// AppendNameValueRow().
//
//     void $zxdb::AppendKeyValueRow(auto key, auto value);
//     void $zxdb::AppendNameValueRow(auto key, auto value);
//
// Appends the given key/value to the list of children of the current FormatNode being formatted.
// This is used when the key comes from the program being debugged.
void AppendNameOrKeyValueRow(const fxl::RefPtr<EvalContext>& eval_context,
                             const std::vector<ExprValue>& params, FormatNode* node,
                             NameQuotes quotes, const FormatOptions& options, EvalCallback cb) {
  if (params.size() != 2u)
    return cb(Err("$zxdb::Append*ValueRow() expects two arguments."));

  // First fill in the child with no name.
  node->children().push_back(std::make_unique<FormatNode>(std::string(), std::move(params[1])));

  // Asynchronously fill in the name (it may need evaluating).
  PopulateName(eval_context, node->children().back()->GetWeakPtr(), params[0], quotes, options,
               std::move(cb));
}

// Implementation of the built-in pretty-printer function AppendKeyRow().
//
//     void $zxdb::AppendNameRow(auto name);
//
// Appends the given name to the list of children of the current FormatNode being formatted. Unlike
// AppendNameValueRow(), this will have no value (which would be appear in the output differently
// than, for example, nullptr or empty string). This can be useful to append things like "..." to
// the end of truncated arrays.
void AppendNameRow(const fxl::RefPtr<EvalContext>& eval_context,
                   const std::vector<ExprValue>& params, FormatNode* node,
                   const FormatOptions& options, EvalCallback cb) {
  if (params.size() != 1u)
    return cb(Err("$zxdb::AppendNameRow() expects one argument."));

  // First fill in the child with no name.
  node->children().push_back(std::make_unique<FormatNode>(std::string()));

  // Asynchronously fill in the name (it may need evaluating).
  PopulateName(eval_context, node->children().back()->GetWeakPtr(), params[0], NameQuotes::kStrip,
               options, std::move(cb));
}

// Implementation of the built-in pretty-printer function GetMaxArraySize().
//
//     int $zxdb::GetMaxArraySize();
//
// This function returns the maximum number of children that a pretty-printer for a container type
// should generate. Otherwise, things can easily get too long and slow. Using this value instead of
// hard-coding a limit allows the user to override the value consistently if they want more items.
//
// If a pretty-printer for a container stops populating items early because it hit the max array
// size, it should call:
//   $zxdb::AppendNameRow("...");
// to make clear that the output was truncated.
void GetMaxArraySize(const fxl::RefPtr<EvalContext>& eval_context,
                     const std::vector<ExprValue>& params, const FormatOptions& options,
                     EvalCallback cb) {
  if (!params.empty())
    return cb(Err("$zxdb::GetMaxArraySize() expects no arguments."));
  return cb(ExprValue(options.max_array_size));
}

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
  FindNameContext GetFindNameContext() const override;
  void GetNamedValue(const ParsedIdentifier& name, EvalCallback cb) const override;
  void GetVariableValue(fxl::RefPtr<Value> variable, EvalCallback cb) const override {
    return impl_->GetVariableValue(std::move(variable), std::move(cb));
  }
  const BuiltinFuncCallback* GetBuiltinFunction(const ParsedIdentifier& name) const override;
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

  // Use the node variant to enable use-cases where the implementation of the formatter may
  // add children to the final ExprNode. The value to be formatted is in node->value().
  PrettyEvalContext(fxl::RefPtr<EvalContext> impl, FormatNode* node,
                    const FormatOptions& options = FormatOptions())
      : impl_(std::move(impl)),
        weak_node_(node->GetWeakPtr()),
        value_(node->value()),
        format_options_(options) {
    AddBuiltinFuncs();
    FillFakeMemberFn();
  }

  // This variant does not support any mutation of the output node. This is used for more narrowly
  // defined cases and only take the thing to be formatted.
  PrettyEvalContext(fxl::RefPtr<EvalContext> impl, ExprValue value,
                    const FormatOptions& options = FormatOptions())
      : impl_(std::move(impl)), value_(std::move(value)), format_options_(options) {
    AddBuiltinFuncs();
    FillFakeMemberFn();
  }

  ~PrettyEvalContext() override = default;

  // Populates the builtin_funcs_ map.
  void AddBuiltinFuncs();

  // Populates fake_member_fn_.
  void FillFakeMemberFn();

  fxl::RefPtr<EvalContext> impl_;

  fxl::WeakPtr<FormatNode> weak_node_;
  ExprValue value_;
  FormatOptions format_options_;

  // A function symbol we've synthesized to make FindName implicitly search the object we're
  // pretty-printing for values and types. This function is made with a "this" variable whose type
  // refers to the type being pretty-printed.
  fxl::RefPtr<Function> fake_member_fn_;

  std::map<ParsedIdentifier, BuiltinFuncCallback> builtin_funcs_;
};

void PrettyEvalContext::FindName(const FindNameOptions& options,
                                 const ParsedIdentifier& looking_for,
                                 std::vector<FoundName>* results) const {
  return ::zxdb::FindName(GetFindNameContext(), options, looking_for, results);
}

FindNameContext PrettyEvalContext::GetFindNameContext() const {
  // The block comes from the fake member function we made. Everything else comes from the
  // surrounding context.
  FindNameContext context = impl_->GetFindNameContext();
  context.block = fake_member_fn_.get();
  return context;
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

const EvalContext::BuiltinFuncCallback* PrettyEvalContext::GetBuiltinFunction(
    const ParsedIdentifier& name) const {
  if (auto found = builtin_funcs_.find(name); found != builtin_funcs_.end())
    return &found->second;
  return nullptr;
}

void PrettyEvalContext::AddBuiltinFuncs() {
  builtin_funcs_[ZxdbNamespaced("AppendKeyValueRow")] =
      [weak_node = weak_node_, format_options = format_options_](
          const fxl::RefPtr<EvalContext>& eval_context, const std::vector<ExprValue>& params,
          EvalCallback cb) {
        if (weak_node) {
          AppendNameOrKeyValueRow(eval_context, params, weak_node.get(), NameQuotes::kKeep,
                                  format_options, std::move(cb));
        } else {
          cb(Err("Value gone"));
        }
      };
  builtin_funcs_[ZxdbNamespaced("AppendNameValueRow")] =
      [weak_node = weak_node_, format_options = format_options_](
          const fxl::RefPtr<EvalContext>& eval_context, const std::vector<ExprValue>& params,
          EvalCallback cb) {
        if (weak_node) {
          AppendNameOrKeyValueRow(eval_context, params, weak_node.get(), NameQuotes::kStrip,
                                  format_options, std::move(cb));
        } else {
          cb(Err("Value gone"));
        }
      };
  builtin_funcs_[ZxdbNamespaced("AppendNameRow")] =
      [weak_node = weak_node_, format_options = format_options_](
          const fxl::RefPtr<EvalContext>& eval_context, const std::vector<ExprValue>& params,
          EvalCallback cb) {
        if (weak_node) {
          AppendNameRow(eval_context, params, weak_node.get(), format_options, std::move(cb));
        } else {
          cb(Err("Value gone"));
        }
      };
  builtin_funcs_[ZxdbNamespaced("GetMaxArraySize")] =
      [format_options = format_options_](const fxl::RefPtr<EvalContext>& eval_context,
                                         const std::vector<ExprValue>& params, EvalCallback cb) {
        GetMaxArraySize(eval_context, params, format_options, std::move(cb));
      };
}

void PrettyEvalContext::FillFakeMemberFn() {
  // See the declaration of fake_member_fn_ above for more.
  fake_member_fn_ = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);

  // Make a DWARF expression that evaluates to the data of the variable being pretty-printed. This
  // isn't strictly necessary as of this writing because the variable data is never used, only the
  // type is used for FindName while values go through GetNamedValue() which doesn't use this
  // code path.
  //
  // This data is being provided for completeness to avoid weird effects of a technically-invalid
  // Variable in case something is changed in the future around member finding. This is implemented
  // using the DW_OP_piece opcode which is followed by the byte count (ULEB-encoded) and that
  // number of bytes of the object data.
  const std::vector<uint8_t>& source_data = value_.data().bytes();
  std::vector<uint8_t> location_expr_data;
  location_expr_data.push_back(llvm::dwarf::DW_OP_piece);
  AppendULeb(source_data.size(), &location_expr_data);
  location_expr_data.insert(location_expr_data.end(), source_data.begin(), source_data.end());
  DwarfExpr location_expr(std::move(location_expr_data));

  // FindName expects "this" to be a pointer type on the block.
  auto this_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, value_.type());
  auto this_var = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable, "this", this_ptr,
                                                VariableLocation(std::move(location_expr)));
  fake_member_fn_->set_object_pointer(LazySymbol(std::move(this_var)));
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
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node, options);

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

void PrettyGenericContainer::Format(FormatNode* node, const FormatOptions& options,
                                    const fxl::RefPtr<EvalContext>& context,
                                    fit::deferred_callback cb) {
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node, options);

  // Format this as a collection which will just be a list of key/value pairs. This pretty-printer
  // will be used for things like maps and sets which will have different requirements.
  node->set_description_kind(FormatNode::kCollection);

  EvalExpression(
      expand_expr_, pretty_context, true,
      [weak_node = node->GetWeakPtr(), cb = std::move(cb)](ErrOrValue result) mutable {
        // The callback will get issued automatically whwen it goes out of scope.
        if (result.has_error()) {
          if (weak_node) {
            weak_node->SetDescribedError(Err("Error pretty-printing: " + result.err().msg()));
          }
        }
      });
}

void PrettyHeapString::Format(FormatNode* node, const FormatOptions& options,
                              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) {
  // Evaluate the expressions with this context to make the members in the current scope.
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value(), options);

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
  auto pretty_context = fxl::MakeRefCounted<PrettyEvalContext>(context, node->value(), options);

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

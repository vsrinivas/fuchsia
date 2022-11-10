// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_

#include <map>
#include <string>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

class MockEvalContext : public EvalContext {
 public:
  // Construct with fxl::MakeRefCounted().

  MockSymbolDataProvider* data_provider() { return data_provider_.get(); }
  PrettyTypeManager& pretty_type_manager() { return pretty_type_manager_; }

  void set_language(ExprLanguage lang) { language_ = lang; }
  void set_abi(std::shared_ptr<Abi> abi) { abi_ = std::move(abi); }
  void set_vector_register_format(VectorRegisterFormat fmt) { vector_register_format_ = fmt; }
  void set_should_promote_to_derived(bool p) { should_promote_to_derived_ = p; }

  // Adds a result to the mocked data returned by FindName.
  void AddName(const ParsedIdentifier& ident, FoundName found);

  // Adds the given mocked variable with the given name and value. If using the "Value" variant, the
  // checked thing is the actual pointer value, not the name.
  //
  // This also registers a result via AddName. The FoundName will be a Variable type.
  //
  // IMPORTANT: This Variable registered for the mocked FindName() will have the name set but
  // nothing else. This means it won't be quite a perfect mock, but is sufficient for knowing that
  // a variable with that name exists (for the parser).
  void AddVariable(const std::string& name, ExprValue v);
  void AddVariable(const Value* key, ExprValue v);

  // Adds a location result for GetLocationForAddress().
  void AddLocation(uint64_t address, Location location);

  // Adds a builtin function.
  void AddBuiltinFunction(const ParsedIdentifier& name, BuiltinFuncCallback impl);

  // EvalContext implementation.
  ExprLanguage GetLanguage() const override { return language_; }
  const std::shared_ptr<Abi>& GetAbi() const override { return abi_; }
  using EvalContext::FindName;
  void FindName(const FindNameOptions& options, const ParsedIdentifier& looking_for,
                std::vector<FoundName>* results) const override;
  FindNameContext GetFindNameContext() const override;
  void GetNamedValue(const ParsedIdentifier& ident, EvalCallback cb) const override;
  void GetVariableValue(fxl::RefPtr<Value> variable, EvalCallback cb) const override;
  const BuiltinFuncCallback* GetBuiltinFunction(const ParsedIdentifier& name) const override;
  const ProcessSymbols* GetProcessSymbols() const override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;
  Location GetLocationForAddress(uint64_t address) const override;
  const PrettyTypeManager& GetPrettyTypeManager() const override { return pretty_type_manager_; }
  VectorRegisterFormat GetVectorRegisterFormat() const override { return vector_register_format_; }
  bool ShouldPromoteToDerived() const override { return should_promote_to_derived_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(MockEvalContext);
  FRIEND_MAKE_REF_COUNTED(MockEvalContext);

  MockEvalContext();
  ~MockEvalContext();

 private:
  std::shared_ptr<Abi> abi_;
  fxl::RefPtr<MockSymbolDataProvider> data_provider_;

  // Mocked results for FindName.
  std::map<ParsedIdentifier, FoundName> names_;

  std::map<std::string, ExprValue> values_by_name_;
  std::map<const Value*, ExprValue> values_by_symbol_;
  std::map<uint64_t, Location> locations_;
  std::map<ParsedIdentifier, BuiltinFuncCallback> builtin_funcs_;
  ExprLanguage language_ = ExprLanguage::kC;
  PrettyTypeManager pretty_type_manager_;
  VectorRegisterFormat vector_register_format_ = VectorRegisterFormat::kDouble;
  bool should_promote_to_derived_ = true;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_

#include "fidl/flat/compiler.h"

namespace fidl::flat {

// We run a separate ConsumeStep for each file in the library.
class ConsumeStep : public Compiler::Step {
 public:
  explicit ConsumeStep(Compiler* compiler, std::unique_ptr<raw::File> file)
      : Step(compiler), file_(std::move(file)) {}

 private:
  void RunImpl() override;

  // Returns a pointer to the registered decl, or null on failure.
  Decl* RegisterDecl(std::unique_ptr<Decl> decl);

  // Top level declarations
  void ConsumeAliasDeclaration(std::unique_ptr<raw::AliasDeclaration> alias_declaration);
  void ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
  void ConsumeProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration);
  void ConsumeResourceDeclaration(std::unique_ptr<raw::ResourceDeclaration> resource_declaration);
  void ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl);
  void ConsumeTypeDecl(std::unique_ptr<raw::TypeDecl> type_decl);
  void ConsumeUsing(std::unique_ptr<raw::Using> using_directive);

  // Layouts
  template <typename T>  // T should be Table or Union
  bool ConsumeOrdinaledLayout(std::unique_ptr<raw::Layout> layout,
                              const std::shared_ptr<NamingContext>& context,
                              std::unique_ptr<raw::AttributeList> raw_attribute_list,
                              Decl** out_decl);
  bool ConsumeStructLayout(std::unique_ptr<raw::Layout> layout,
                           const std::shared_ptr<NamingContext>& context,
                           std::unique_ptr<raw::AttributeList> raw_attribute_list, Decl** out_decl);
  template <typename T>  // T should be Bits or Enum
  bool ConsumeValueLayout(std::unique_ptr<raw::Layout> layout,
                          const std::shared_ptr<NamingContext>& context,
                          std::unique_ptr<raw::AttributeList> raw_attribute_list, Decl** out_decl);
  bool ConsumeLayout(std::unique_ptr<raw::Layout> layout,
                     const std::shared_ptr<NamingContext>& context,
                     std::unique_ptr<raw::AttributeList> raw_attribute_list, Decl** out_decl);

  // Other elements
  void ConsumeAttribute(std::unique_ptr<raw::Attribute> raw_attribute,
                        std::unique_ptr<Attribute>* out_attribute);
  void ConsumeAttributeList(std::unique_ptr<raw::AttributeList> raw_attribute_list,
                            std::unique_ptr<AttributeList>* out_attribute_list);
  bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant,
                       std::unique_ptr<Constant>* out_constant);
  void ConsumeLiteralConstant(raw::LiteralConstant* raw_constant,
                              std::unique_ptr<LiteralConstant>* out_constant);
  bool ConsumeParameterList(SourceSpan method_name, const std::shared_ptr<NamingContext>& context,
                            std::unique_ptr<raw::ParameterList> parameter_layout,
                            bool is_request_or_response,
                            std::unique_ptr<TypeConstructor>* out_payload);
  bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                              const std::shared_ptr<NamingContext>& context,
                              std::unique_ptr<raw::AttributeList> raw_attribute_list,

                              std::unique_ptr<TypeConstructor>* out_type, Decl** out_inline_decl);
  bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                              const std::shared_ptr<NamingContext>& context,
                              std::unique_ptr<TypeConstructor>* out_type);

  // Sets the naming context's generated name override to the @generated_name
  // attribute's value if present, otherwise does nothing.
  void MaybeOverrideName(AttributeList& attributes, NamingContext* context);
  // Attempts to resolve the compound identifier to a name within the context of
  // a library. On failure, reports an errro and returns null.
  std::optional<Name> CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier);
  bool CreateMethodResult(const std::shared_ptr<NamingContext>& success_variant_context,
                          const std::shared_ptr<NamingContext>& err_variant_context,
                          SourceSpan response_span, raw::ProtocolMethod* method,
                          std::unique_ptr<TypeConstructor> success_variant,
                          std::unique_ptr<TypeConstructor>* out_payload);

  std::unique_ptr<raw::File> file_;

  // This map contains a subset of library_->declarations_ (no imported decls)
  // keyed by `utils::canonicalize(name.decl_name())` rather than `name.key()`.
  std::map<std::string, const Decl*> declarations_by_canonical_name_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_

#include "tools/fidl/fidlc/include/fidl/flat/compiler.h"

namespace fidl::flat {

// We run a separate ConsumeStep for each file in the library.
class ConsumeStep : public Compiler::Step {
 public:
  explicit ConsumeStep(Compiler* compiler, std::unique_ptr<raw::File> file);

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
  void ConsumeNewType(std::unique_ptr<raw::TypeDecl> type_decl);
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

  // Elements stored in the library
  const raw::Literal* ConsumeLiteral(std::unique_ptr<raw::Literal> raw_literal);
  const raw::Identifier* ConsumeIdentifier(std::unique_ptr<raw::Identifier> raw_identifier);
  const raw::Ordinal64* ConsumeOrdinal(std::unique_ptr<raw::Ordinal64> raw_ordinal);

  // Sets the naming context's generated name override to the @generated_name
  // attribute's value if present, otherwise does nothing.
  void MaybeOverrideName(AttributeList& attributes, NamingContext* context);
  // Generates the synthetic result type used for encoding the method's response, if the method has
  // an error type or is marked as flexible (or both). Adds the generated type to the library and
  // provides a `flat::TypeConstructor` that refers to it.
  //
  // The generated type includes both the outer wrapping struct and the result union.
  bool CreateMethodResult(const std::shared_ptr<NamingContext>& success_variant_context,
                          const std::shared_ptr<NamingContext>& err_variant_context,
                          const std::shared_ptr<NamingContext>& transport_err_variant_context,
                          bool has_err, bool has_transport_err, SourceSpan response_span,
                          raw::ProtocolMethod* method,
                          std::unique_ptr<TypeConstructor> success_variant,
                          std::unique_ptr<TypeConstructor>* out_payload);

  std::unique_ptr<raw::File> file_;

  // Decl for default underlying type to use for bits and enums.
  Decl* default_underlying_type_;

  // Decl for the type to use for transport_err.
  Decl* transport_err_type_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_CONSUME_STEP_H_

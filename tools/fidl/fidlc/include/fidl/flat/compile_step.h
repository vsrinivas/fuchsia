// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILE_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILE_STEP_H_

#include "tools/fidl/fidlc/include/fidl/flat/compiler.h"

namespace fidl::flat {

// We run one main CompileStep for the whole library. Some attributes are
// compiled before that via the CompileAttributeEarly method. To avoid kicking
// off other compilations, these attributes only allow literal arguments.
class CompileStep : public Compiler::Step {
 public:
  using Step::Step;

  friend class AttributeSchema;
  friend class AttributeArgSchema;
  friend class TypeResolver;

  // Compiles an attribute early, before the main CompileStep has started. The
  // attribute must support this (see AttributeSchema::CanCompileEarly).
  static void CompileAttributeEarly(Compiler* compiler, Attribute* attribute);

 private:
  void RunImpl() override;

  // Compile methods
  void CompileAlias(Alias* alias);
  void CompileAttribute(Attribute* attribute, bool early = false);
  void CompileAttributeList(AttributeList* attributes);
  void CompileBits(Bits* bits_declaration);
  void CompileConst(Const* const_declaration);
  void CompileDecl(Decl* decl);
  void CompileEnum(Enum* enum_declaration);
  void CompileNewType(NewType* new_type);
  void CompileProtocol(Protocol* protocol_declaration);
  void CompileResource(Resource* resource_declaration);
  void CompileService(Service* service_decl);
  void CompileStruct(Struct* struct_declaration);
  void CompileTable(Table* table_declaration);
  void CompileTypeConstructor(TypeConstructor* type_ctor);
  void CompileUnion(Union* union_declaration);

  // Resolve methods
  bool ResolveHandleRightsConstant(Resource* resource, Constant* constant,
                                   const HandleRights** out_rights);
  bool ResolveHandleSubtypeIdentifier(Resource* resource, Constant* constant,
                                      uint32_t* out_obj_type);
  bool ResolveSizeBound(Constant* size_constant, const Size** out_size);
  bool ResolveOrOperatorConstant(Constant* constant, std::optional<const Type*> opt_type,
                                 const ConstantValue& left_operand,
                                 const ConstantValue& right_operand);
  bool ResolveConstant(Constant* constant, std::optional<const Type*> opt_type);
  bool ResolveIdentifierConstant(IdentifierConstant* identifier_constant,
                                 std::optional<const Type*> opt_type);
  bool ResolveLiteralConstant(LiteralConstant* literal_constant,
                              std::optional<const Type*> opt_type);
  bool ResolveAsOptional(Constant* constant);
  template <typename NumericType>
  bool ResolveLiteralConstantKindNumericLiteral(LiteralConstant* literal_constant,
                                                const Type* type);

  // Type methods
  bool TypeCanBeConst(const Type* type);
  bool TypeIsConvertibleTo(const Type* from_type, const Type* to_type);
  const Type* UnderlyingType(const Type* type);
  const Type* InferType(Constant* constant);
  ConstantValue::Kind ConstantValuePrimitiveKind(types::PrimitiveSubtype primitive_subtype);

  // Validates a single member of a bits or enum. On success, returns nullptr,
  // and on failure returns an error. The caller will set the diagnostic span.
  template <typename MemberType>
  using MemberValidator = fit::function<std::unique_ptr<Diagnostic>(
      const MemberType& member, const AttributeList* attributes, SourceSpan span)>;

  // Validation methods
  template <typename DeclType, typename MemberType>
  bool ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator);
  template <typename MemberType>
  bool ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask);
  template <typename MemberType>
  bool ValidateEnumMembersAndCalcUnknownValue(Enum* enum_decl, MemberType* out_unknown_value);

  // Decl for the HEAD constant, used in attribute_schema.cc.
  Decl* head_decl;

  // If the given |decl| is already in the decl_stack, gets a vector of decls
  // describing the decl cycle starting and ending with that decl. Otherwise,
  // returns nullopt.
  std::optional<std::vector<const Decl*>> GetDeclCycle(const Decl* decl);

  // Stack of decls being compiled. Used to trace back and print the cycle if a
  // cycle is detected.
  std::vector<const Decl*> decl_stack_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILE_STEP_H_

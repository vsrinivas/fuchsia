// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_

#include "fidl/flat/name.h"
#include "fidl/flat/types.h"
#include "fidl/reporter.h"

namespace fidl::flat {

constexpr uint32_t kHandleSameRights = 0x80000000;  // ZX_HANDLE_SAME_RIGHTS

class TypeResolver;
class Typespace;

struct LayoutInvocation;
struct LayoutParameterList;
struct TypeAlias;
struct TypeConstraints;

class TypeTemplate : protected ReporterMixin {
 public:
  TypeTemplate(Name name, Typespace* typespace, Reporter* reporter)
      : ReporterMixin(reporter), typespace_(typespace), name_(std::move(name)) {}

  TypeTemplate(TypeTemplate&& type_template) = default;

  virtual ~TypeTemplate() = default;

  const Name& name() const { return name_; }

  struct ParamsAndConstraints {
    const std::unique_ptr<LayoutParameterList>& parameters;
    const std::unique_ptr<TypeConstraints>& constraints;
    const std::optional<SourceSpan>& type_ctor_span;

    // If we have some parameters, then return the span of that.
    // If we have no parameters and we have type_ctor_span available, we return
    // that.
    // Otherwise, we return the empty span corresponding to no parameters.
    const SourceSpan& ParametersSpan() const;
  };

  virtual bool Create(TypeResolver* resolver, const ParamsAndConstraints& args,
                      std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const = 0;

  bool HasGeneratedName() const;

 protected:
  Typespace* typespace_;

  Name name_;

  // Returns false if there was an error (which is reported), true otherwise.
  bool EnsureNumberOfLayoutParams(const ParamsAndConstraints& unresolved_args,
                                  size_t expected_params) const;
};

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
//
// TODO(fxbug.dev/76219): Implement canonicalization.
class Typespace : private ReporterMixin {
 public:
  explicit Typespace(Reporter* reporter) : ReporterMixin(reporter) {}

  bool Create(TypeResolver* resolver, const flat::Name& name,
              const std::unique_ptr<LayoutParameterList>& parameters,
              const std::unique_ptr<TypeConstraints>& constraints, const Type** out_type,
              LayoutInvocation* out_params, const std::optional<SourceSpan>& type_ctor_span);

  const Size* InternSize(uint32_t size);
  const Type* Intern(std::unique_ptr<Type> type);

  void AddTemplate(std::unique_ptr<TypeTemplate> type_template);

  // RootTypes creates a instance with all primitive types. It is meant to be
  // used as the top-level types lookup mechanism, providing definitional
  // meaning to names such as `int64`, or `bool`.
  static Typespace RootTypes(Reporter* reporter);

  static const PrimitiveType kBoolType;
  static const PrimitiveType kInt8Type;
  static const PrimitiveType kInt16Type;
  static const PrimitiveType kInt32Type;
  static const PrimitiveType kInt64Type;
  static const PrimitiveType kUint8Type;
  static const PrimitiveType kUint16Type;
  static const PrimitiveType kUint32Type;
  static const PrimitiveType kUint64Type;
  static const PrimitiveType kFloat32Type;
  static const PrimitiveType kFloat64Type;
  static const UntypedNumericType kUntypedNumericType;
  static const StringType kUnboundedStringType;

 private:
  friend class TypeAliasTypeTemplate;

  const TypeTemplate* LookupTemplate(const flat::Name& name) const;

  bool CreateNotOwned(TypeResolver* resolver, const flat::Name& name,
                      const std::unique_ptr<LayoutParameterList>& parameters,
                      const std::unique_ptr<TypeConstraints>& constraints,
                      std::unique_ptr<Type>* out_type, LayoutInvocation* out_params,
                      const std::optional<SourceSpan>& type_ctor_span);

  std::map<Name::Key, std::unique_ptr<TypeTemplate>> templates_;
  std::vector<std::unique_ptr<Size>> sizes_;
  std::vector<std::unique_ptr<Type>> types_;

  static const Name kBoolTypeName;
  static const Name kInt8TypeName;
  static const Name kInt16TypeName;
  static const Name kInt32TypeName;
  static const Name kInt64TypeName;
  static const Name kUint8TypeName;
  static const Name kUint16TypeName;
  static const Name kUint32TypeName;
  static const Name kUint64TypeName;
  static const Name kFloat32TypeName;
  static const Name kFloat64TypeName;
  static const Name kUntypedNumericTypeName;
  static const Name kStringTypeName;
};

class ArrayTypeTemplate final : public TypeTemplate {
 public:
  ArrayTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("array"), typespace, reporter) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;
};

class BytesTypeTemplate final : public TypeTemplate {
 public:
  BytesTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter),
        uint8_type_(kUint8Type) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  // TODO(fxbug.dev/7724): Remove when canonicalizing types.
  const Name kUint8TypeName = Name::CreateIntrinsic("uint8");
  const PrimitiveType kUint8Type = PrimitiveType(kUint8TypeName, types::PrimitiveSubtype::kUint8);

  const PrimitiveType uint8_type_;
};

class VectorTypeTemplate final : public TypeTemplate {
 public:
  VectorTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;
};

class StringTypeTemplate final : public TypeTemplate {
 public:
  StringTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("string"), typespace, reporter) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;
};

class HandleTypeTemplate final : public TypeTemplate {
 public:
  HandleTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, Resource* resource_decl_)
      : TypeTemplate(std::move(name), typespace, reporter), resource_decl_(resource_decl_) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  const static HandleRights kSameRights;

  Resource* resource_decl_;
};

class TransportSideTypeTemplate final : public TypeTemplate {
 public:
  TransportSideTypeTemplate(Typespace* typespace, Reporter* reporter, TransportSide end,
                            std::string_view protocol_transport)
      : TypeTemplate(end == TransportSide::kClient ? Name::CreateIntrinsic("client_end")
                                                   : Name::CreateIntrinsic("server_end"),
                     typespace, reporter),
        end_(end),
        protocol_transport_(protocol_transport) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  TransportSide end_;
  std::string_view protocol_transport_;
};

class TypeDeclTypeTemplate final : public TypeTemplate {
 public:
  TypeDeclTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, TypeDecl* type_decl)
      : TypeTemplate(std::move(name), typespace, reporter), type_decl_(type_decl) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  TypeDecl* type_decl_;
};

class TypeAliasTypeTemplate final : public TypeTemplate {
 public:
  TypeAliasTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, TypeAlias* decl)
      : TypeTemplate(std::move(name), typespace, reporter), decl_(decl) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  TypeAlias* decl_;
};

class BoxTypeTemplate final : public TypeTemplate {
 public:
  BoxTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("box"), typespace, reporter) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;
};

class PrimitiveTypeTemplate : public TypeTemplate {
 public:
  PrimitiveTypeTemplate(Typespace* typespace, Reporter* reporter, std::string name,
                        types::PrimitiveSubtype subtype)
      : TypeTemplate(Name::CreateIntrinsic(std::move(name)), typespace, reporter),
        subtype_(subtype) {}

  bool Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override;

 private:
  const types::PrimitiveSubtype subtype_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPESPACE_H_

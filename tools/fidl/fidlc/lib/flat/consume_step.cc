// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/consume_step.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/include/fidl/flat/compile_step.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/raw_ast.h"

namespace fidl::flat {

ConsumeStep::ConsumeStep(Compiler* compiler, std::unique_ptr<raw::File> file)
    : Step(compiler),
      file_(std::move(file)),
      default_underlying_type_(
          all_libraries()->root_library()->declarations.LookupBuiltin(Builtin::Identity::kUint32)),
      transport_err_type_(all_libraries()->root_library()->declarations.LookupBuiltin(
          Builtin::Identity::kTransportErr)) {}

void ConsumeStep::RunImpl() {
  // All fidl files in a library should agree on the library name.
  std::vector<std::string_view> new_name;
  for (const auto& part : file_->library_decl->path->components) {
    new_name.push_back(part->span().data());
  }
  if (library()->name.empty()) {
    library()->name = new_name;
    library()->arbitrary_name_span = file_->library_decl->span();
  } else {
    if (new_name != library()->name) {
      Fail(ErrFilesDisagreeOnLibraryName, file_->library_decl->path->components[0]->span());
      return;
    }
    // Prefer setting arbitrary_name_span to a file which has attributes on the
    // library declaration, if any do, since it's conventional to put all
    // library attributes and the doc comment in a single file (overview.fidl).
    if (library()->attributes->Empty() && file_->library_decl->attributes) {
      library()->arbitrary_name_span = file_->library_decl->span();
    }
  }
  library()->library_name_declarations.emplace_back(file_->library_decl->path->span());

  ConsumeAttributeList(std::move(file_->library_decl->attributes), &library()->attributes);

  for (auto& using_directive : std::move(file_->using_list)) {
    ConsumeUsing(std::move(using_directive));
  }
  for (auto& alias_declaration : std::move(file_->alias_list)) {
    ConsumeAliasDeclaration(std::move(alias_declaration));
  }
  for (auto& const_declaration : std::move(file_->const_declaration_list)) {
    ConsumeConstDeclaration(std::move(const_declaration));
  }
  for (auto& protocol_declaration : std::move(file_->protocol_declaration_list)) {
    ConsumeProtocolDeclaration(std::move(protocol_declaration));
  }
  for (auto& resource_declaration : std::move(file_->resource_declaration_list)) {
    ConsumeResourceDeclaration(std::move(resource_declaration));
  }
  for (auto& service_declaration : std::move(file_->service_declaration_list)) {
    ConsumeServiceDeclaration(std::move(service_declaration));
  }
  for (auto& type_decl : std::move(file_->type_decls)) {
    ConsumeTypeDecl(std::move(type_decl));
  }
}

Decl* ConsumeStep::RegisterDecl(std::unique_ptr<Decl> decl) {
  auto decl_ptr = library()->declarations.Insert(std::move(decl));
  const Name& name = decl_ptr->name;
  if (name.span()) {
    if (library()->dependencies.Contains(name.span()->source_file().filename(),
                                         {name.span()->data()})) {
      Fail(ErrDeclNameConflictsWithLibraryImport, name.span().value(), name);
    } else if (auto canonical_decl_name = utils::canonicalize(name.decl_name());
               library()->dependencies.Contains(name.span()->source_file().filename(),
                                                {canonical_decl_name})) {
      Fail(ErrDeclNameConflictsWithLibraryImportCanonical, name.span().value(), name,
           canonical_decl_name);
    }
  }
  return decl_ptr;
}

void ConsumeStep::ConsumeAttributeList(std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                       std::unique_ptr<AttributeList>* out_attribute_list) {
  ZX_ASSERT_MSG(out_attribute_list, "must provide out parameter");
  // Usually *out_attribute_list is null and we create the AttributeList here.
  // For library declarations it's not, since we consume attributes from each
  // file into the same library->attributes field.
  if (*out_attribute_list == nullptr) {
    *out_attribute_list = std::make_unique<AttributeList>();
  }
  if (!raw_attribute_list) {
    return;
  }
  auto& out_attributes = (*out_attribute_list)->attributes;
  for (auto& raw_attribute : raw_attribute_list->attributes) {
    std::unique_ptr<Attribute> attribute;
    ConsumeAttribute(std::move(raw_attribute), &attribute);
    out_attributes.push_back(std::move(attribute));
  }
}

void ConsumeStep::ConsumeAttribute(std::unique_ptr<raw::Attribute> raw_attribute,
                                   std::unique_ptr<Attribute>* out_attribute) {
  bool all_named = true;
  std::vector<std::unique_ptr<AttributeArg>> args;
  for (auto& raw_arg : raw_attribute->args) {
    std::unique_ptr<Constant> constant;
    if (!ConsumeConstant(std::move(raw_arg->value), &constant)) {
      continue;
    }
    std::optional<SourceSpan> name;
    if (raw_arg->maybe_name) {
      name = raw_arg->maybe_name->span();
    }
    all_named = all_named && name.has_value();
    args.emplace_back(std::make_unique<AttributeArg>(name, std::move(constant), raw_arg->span()));
  }
  ZX_ASSERT_MSG(all_named || args.size() == 1,
                "parser should not allow an anonymous arg with other args");
  SourceSpan name;
  switch (raw_attribute->provenance) {
    case raw::Attribute::Provenance::kDefault:
      name = raw_attribute->maybe_name->span();
      break;
    case raw::Attribute::Provenance::kDocComment:
      name = generated_source_file()->AddLine(Attribute::kDocCommentName);
      break;
  }
  *out_attribute = std::make_unique<Attribute>(name, std::move(args), raw_attribute->span());
  all_libraries()->WarnOnAttributeTypo(out_attribute->get());
}

bool ConsumeStep::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant,
                                  std::unique_ptr<Constant>* out_constant) {
  switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
      auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
      *out_constant =
          std::make_unique<IdentifierConstant>(*identifier->identifier, identifier->span());
      break;
    }
    case raw::Constant::Kind::kLiteral: {
      auto literal = static_cast<raw::LiteralConstant*>(raw_constant.get());
      std::unique_ptr<LiteralConstant> out;
      ConsumeLiteralConstant(literal, &out);
      *out_constant = std::unique_ptr<Constant>(out.release());
      break;
    }
    case raw::Constant::Kind::kBinaryOperator: {
      auto binary_operator_constant = static_cast<raw::BinaryOperatorConstant*>(raw_constant.get());
      BinaryOperatorConstant::Operator op;
      switch (binary_operator_constant->op) {
        case raw::BinaryOperatorConstant::Operator::kOr:
          op = BinaryOperatorConstant::Operator::kOr;
          break;
      }
      std::unique_ptr<Constant> left_operand;
      if (!ConsumeConstant(std::move(binary_operator_constant->left_operand), &left_operand)) {
        return false;
      }
      std::unique_ptr<Constant> right_operand;
      if (!ConsumeConstant(std::move(binary_operator_constant->right_operand), &right_operand)) {
        return false;
      }
      *out_constant = std::make_unique<BinaryOperatorConstant>(
          std::move(left_operand), std::move(right_operand), op, binary_operator_constant->span());
      break;
    }
  }
  return true;
}

void ConsumeStep::ConsumeLiteralConstant(raw::LiteralConstant* raw_constant,
                                         std::unique_ptr<LiteralConstant>* out_constant) {
  *out_constant =
      std::make_unique<LiteralConstant>(ConsumeLiteral(std::move(raw_constant->literal)));
}

void ConsumeStep::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
  if (using_directive->attributes != nullptr) {
    Fail(ErrAttributesNotAllowedOnLibraryImport, using_directive->span(),
         using_directive->attributes.get());
    return;
  }

  std::vector<std::string_view> library_name;
  for (const auto& component : using_directive->using_path->components) {
    library_name.push_back(component->span().data());
  }

  Library* dep_library = all_libraries()->Lookup(library_name);
  if (!dep_library) {
    Fail(ErrUnknownLibrary, using_directive->using_path->components[0]->span(), library_name);
    return;
  }

  const auto filename = using_directive->span().source_file().filename();
  const auto result = library()->dependencies.Register(
      using_directive->using_path->span(), filename, dep_library, using_directive->maybe_alias);
  switch (result) {
    case Dependencies::RegisterResult::kSuccess:
      break;
    case Dependencies::RegisterResult::kDuplicate:
      Fail(ErrDuplicateLibraryImport, using_directive->span(), library_name);
      return;
    case Dependencies::RegisterResult::kCollision:
      if (using_directive->maybe_alias) {
        Fail(ErrConflictingLibraryImportAlias, using_directive->span(), library_name,
             using_directive->maybe_alias->span().data());
        return;
      }
      Fail(ErrConflictingLibraryImport, using_directive->span(), library_name);
      return;
  }
}

void ConsumeStep::ConsumeAliasDeclaration(
    std::unique_ptr<raw::AliasDeclaration> alias_declaration) {
  ZX_ASSERT(alias_declaration->alias && alias_declaration->type_ctor != nullptr);

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(alias_declaration->attributes), &attributes);

  auto alias_name = Name::CreateSourced(library(), alias_declaration->alias->span());
  std::unique_ptr<TypeConstructor> type_ctor_;

  if (!ConsumeTypeConstructor(std::move(alias_declaration->type_ctor),
                              NamingContext::Create(alias_name), &type_ctor_))
    return;

  RegisterDecl(std::make_unique<TypeAlias>(std::move(attributes), std::move(alias_name),
                                           std::move(type_ctor_)));
}

void ConsumeStep::ConsumeConstDeclaration(
    std::unique_ptr<raw::ConstDeclaration> const_declaration) {
  auto span = const_declaration->identifier->span();
  auto name = Name::CreateSourced(library(), span);
  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(const_declaration->attributes), &attributes);

  std::unique_ptr<TypeConstructor> type_ctor;
  if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), NamingContext::Create(name),
                              &type_ctor))
    return;

  std::unique_ptr<Constant> constant;
  if (!ConsumeConstant(std::move(const_declaration->constant), &constant))
    return;

  RegisterDecl(std::make_unique<Const>(std::move(attributes), std::move(name), std::move(type_ctor),
                                       std::move(constant)));
}

// Create a type constructor pointing to an anonymous layout.
static std::unique_ptr<TypeConstructor> IdentifierTypeForDecl(Decl* decl) {
  return std::make_unique<TypeConstructor>(Reference(Reference::Target(decl)),
                                           std::make_unique<LayoutParameterList>(),
                                           std::make_unique<TypeConstraints>());
}

bool ConsumeStep::CreateMethodResult(
    const std::shared_ptr<NamingContext>& success_variant_context,
    const std::shared_ptr<NamingContext>& err_variant_context,
    const std::shared_ptr<NamingContext>& transport_err_variant_context, bool has_err,
    bool has_transport_err, SourceSpan response_span, raw::ProtocolMethod* method,
    std::unique_ptr<TypeConstructor> success_variant,
    std::unique_ptr<TypeConstructor>* out_payload) {
  ZX_ASSERT_MSG(
      has_err || has_transport_err,
      "method should only use a result union if it has a result union and/or is flexible");
  ZX_ASSERT(err_variant_context != nullptr);
  ZX_ASSERT(transport_err_variant_context != nullptr);

  raw::SourceElement sourceElement = raw::SourceElement(fidl::Token(), fidl::Token());
  std::vector<Union::Member> result_members;

  enum {
    kSuccessOrdinal = 1,
    kErrorOrdinal = 2,
    kTransportErrorOrdinal = 3,
  };

  result_members.emplace_back(
      ConsumeOrdinal(std::make_unique<raw::Ordinal64>(sourceElement, kSuccessOrdinal)),
      std::move(success_variant), success_variant_context->name(),
      std::make_unique<AttributeList>());

  if (has_err) {
    std::unique_ptr<TypeConstructor> error_type_ctor;
    // Compile the error type.
    if (!ConsumeTypeConstructor(std::move(method->maybe_error_ctor), err_variant_context,
                                &error_type_ctor))
      return false;

    ZX_ASSERT_MSG(error_type_ctor != nullptr, "missing err type ctor");

    result_members.emplace_back(
        ConsumeOrdinal(std::make_unique<raw::Ordinal64>(sourceElement, kErrorOrdinal)),
        std::move(error_type_ctor), err_variant_context->name(), std::make_unique<AttributeList>());
  } else {
    // If there's no error, the error variant is reserved.
    result_members.emplace_back(
        ConsumeOrdinal(std::make_unique<raw::Ordinal64>(sourceElement, kErrorOrdinal)),
        err_variant_context->name(), std::make_unique<AttributeList>());
  }

  if (has_transport_err) {
    std::unique_ptr<TypeConstructor> error_type_ctor = IdentifierTypeForDecl(transport_err_type_);
    ZX_ASSERT_MSG(error_type_ctor != nullptr, "missing transport_err type ctor");
    result_members.emplace_back(
        ConsumeOrdinal(std::make_unique<raw::Ordinal64>(sourceElement, kTransportErrorOrdinal)),
        std::move(error_type_ctor), transport_err_variant_context->name(),
        std::make_unique<AttributeList>());
  }
  // transport_err is not defined if the method is not flexible.

  std::vector<std::unique_ptr<Attribute>> result_attributes;
  result_attributes.emplace_back(
      std::make_unique<Attribute>(generated_source_file()->AddLine("result")));

  // TODO(fxbug.dev/8027): Join spans of response and error constructor for `result_name`.
  auto result_context = err_variant_context->parent();
  auto result_name = Name::CreateAnonymous(library(), response_span, result_context);
  auto union_decl = std::make_unique<Union>(
      std::make_unique<AttributeList>(std::move(result_attributes)), std::move(result_name),
      std::move(result_members), types::Strictness::kStrict, std::nullopt /* resourceness */);
  auto result_decl = union_decl.get();
  if (!RegisterDecl(std::move(union_decl)))
    return false;

  // Make a new response struct for the method containing just the
  // result union.
  std::vector<Struct::Member> response_members;
  response_members.emplace_back(IdentifierTypeForDecl(result_decl), result_context->name(), nullptr,
                                std::make_unique<AttributeList>());

  const auto& response_context = result_context->parent();
  const Name response_name = Name::CreateAnonymous(library(), response_span, response_context);
  auto struct_decl = std::make_unique<Struct>(
      /* attributes = */ std::make_unique<AttributeList>(), response_name,
      std::move(response_members),
      /* resourceness = */ std::nullopt);
  auto payload = IdentifierTypeForDecl(struct_decl.get());
  if (!RegisterDecl(std::move(struct_decl)))
    return false;

  *out_payload = std::move(payload);
  return true;
}

void ConsumeStep::ConsumeProtocolDeclaration(
    std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration) {
  auto protocol_name = Name::CreateSourced(library(), protocol_declaration->identifier->span());
  auto protocol_context = NamingContext::Create(protocol_name.span().value());

  std::vector<Protocol::ComposedProtocol> composed_protocols;
  for (auto& raw_composed : protocol_declaration->composed_protocols) {
    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(raw_composed->attributes), &attributes);
    composed_protocols.emplace_back(std::move(attributes), Reference(*raw_composed->protocol_name));
  }

  std::vector<Protocol::Method> methods;
  for (auto& method : protocol_declaration->methods) {
    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(method->attributes), &attributes);

    auto strictness = types::Strictness::kStrict;
    if (experimental_flags().IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions)) {
      strictness = types::Strictness::kFlexible;
      if (method->modifiers != nullptr && method->modifiers->maybe_strictness.has_value())
        strictness = method->modifiers->maybe_strictness->value;
    }

    SourceSpan method_name = method->identifier->span();
    bool has_request = method->maybe_request != nullptr;
    std::unique_ptr<TypeConstructor> maybe_request;
    if (has_request) {
      bool result = ConsumeParameterList(method_name, protocol_context->EnterRequest(method_name),
                                         std::move(method->maybe_request), true, &maybe_request);
      if (!result)
        return;
    }

    std::unique_ptr<TypeConstructor> maybe_response;
    bool has_response = method->maybe_response != nullptr;
    bool has_error = false;
    if (has_response) {
      has_error = method->maybe_error_ctor != nullptr;
      // has_transport_error is true for flexible two-way methods. We already
      // checked has_response in the outer if block, so to see whether this is a
      // two-way method or an event, we check has_request here.
      bool has_transport_error = has_request && strictness == types::Strictness::kFlexible;

      if (has_error || has_transport_error) {
        SourceSpan response_span = method->maybe_response->span();
        const auto response_context = has_request ? protocol_context->EnterResult(method_name)
                                                  : protocol_context->EnterEvent(method_name);

        std::shared_ptr<NamingContext> result_context, success_variant_context, err_variant_context,
            transport_err_variant_context;
        // TODO(fxbug.dev/95231): remove this comment when outer-most struct is no longer used.
        //
        // The error syntax and flexible methods for protocol P and method M
        // desugars to the following type:
        //
        // // the "response"
        // struct {
        //   // the "result"
        //   result @generated_name("P_M_Result") union {
        //     // the "success variant"
        //     1: response @generated_name("P_M_Response") [user specified response type];
        //     // the "error variant"
        //     2: err @generated_name("P_M_Error") [user specified error type];
        //     3: transport_err  zx.status;
        //   };
        // };
        //
        // If the method is strict, transport_err will not be provided. If the method is flexible
        // but does not have an error, the err variant will be marked as reserved.
        //
        // Note that this can lead to ambiguity with the success variant, since its member
        // name within the union is "response". The naming convention within fidlc
        // is to refer to each type using the name provided in the comments
        // above (i.e. "response" refers to the top level struct, not the success variant).
        //
        // The naming scheme for the result type and the success variant in a response
        // with an error type predates the design of the anonymous name flattening
        // algorithm, and we therefore they are overridden to be backwards compatible.
        result_context = response_context->EnterMember(generated_source_file()->AddLine("result"));
        result_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Result"}, "_"));
        success_variant_context =
            result_context->EnterMember(generated_source_file()->AddLine("response"));
        success_variant_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Response"}, "_"));
        err_variant_context = result_context->EnterMember(generated_source_file()->AddLine("err"));
        err_variant_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Error"}, "_"));
        transport_err_variant_context =
            result_context->EnterMember(generated_source_file()->AddLine("transport_err"));

        std::unique_ptr<TypeConstructor> result_payload;

        if (!ConsumeParameterList(method_name, success_variant_context,
                                  std::move(method->maybe_response), false, &result_payload)) {
          return;
        }

        ZX_ASSERT_MSG(err_variant_context != nullptr && transport_err_variant_context != nullptr,
                      "error type contexts should have been computed");
        if (!CreateMethodResult(success_variant_context, err_variant_context,
                                transport_err_variant_context, has_error, has_transport_error,
                                response_span, method.get(), std::move(result_payload),
                                &maybe_response))
          return;
      } else {
        const auto response_context = has_request ? protocol_context->EnterResponse(method_name)
                                                  : protocol_context->EnterEvent(method_name);
        std::unique_ptr<TypeConstructor> response_payload;
        if (!ConsumeParameterList(method_name, response_context, std::move(method->maybe_response),
                                  true, &response_payload)) {
          return;
        }

        maybe_response = std::move(response_payload);
      }
    }
    ZX_ASSERT(has_request || has_response);
    methods.emplace_back(std::move(attributes), strictness,
                         ConsumeIdentifier(std::move(method->identifier)), method_name, has_request,
                         std::move(maybe_request), has_response, std::move(maybe_response),
                         has_error);
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(protocol_declaration->attributes), &attributes);

  auto openness = types::Openness::kOpen;
  if (protocol_declaration->modifiers != nullptr &&
      protocol_declaration->modifiers->maybe_openness.has_value())
    openness = protocol_declaration->modifiers->maybe_openness->value;

  RegisterDecl(std::make_unique<Protocol>(std::move(attributes), openness, std::move(protocol_name),
                                          std::move(composed_protocols), std::move(methods)));
}

bool ConsumeStep::ConsumeParameterList(const SourceSpan method_name,
                                       const std::shared_ptr<NamingContext>& context,
                                       std::unique_ptr<raw::ParameterList> parameter_layout,
                                       bool is_request_or_response,
                                       std::unique_ptr<TypeConstructor>* out_payload) {
  // If the payload is empty, like the request in `Foo()` or the response in
  // `Foo(...) -> ()` or the success variant in `Foo(...) -> () error uint32`:
  if (!parameter_layout->type_ctor) {
    // If this is not a request or response, but a success variant:
    if (!is_request_or_response) {
      // Fail because we want `Foo(...) -> (struct {}) error uint32` instead.
      return Fail(ErrResponsesWithErrorsMustNotBeEmpty, parameter_layout->span(), method_name);
    }
    // Otherwise, there is nothing to do for an empty payload.
    return true;
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  Decl* inline_decl = nullptr;
  if (!ConsumeTypeConstructor(std::move(parameter_layout->type_ctor), context,
                              /*raw_attribute_list=*/nullptr, &type_ctor, &inline_decl))
    return false;

  *out_payload = std::move(type_ctor);
  return true;
}

void ConsumeStep::ConsumeResourceDeclaration(
    std::unique_ptr<raw::ResourceDeclaration> resource_declaration) {
  auto name = Name::CreateSourced(library(), resource_declaration->identifier->span());
  std::vector<Resource::Property> properties;
  for (auto& property : resource_declaration->properties) {
    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(property->attributes), &attributes);

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(property->type_ctor), NamingContext::Create(name),
                                &type_ctor))
      return;
    properties.emplace_back(std::move(type_ctor), property->identifier->span(),
                            std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(resource_declaration->attributes), &attributes);

  std::unique_ptr<TypeConstructor> type_ctor;
  if (resource_declaration->maybe_type_ctor != nullptr) {
    if (!ConsumeTypeConstructor(std::move(resource_declaration->maybe_type_ctor),
                                NamingContext::Create(name), &type_ctor))
      return;
  } else {
    type_ctor = IdentifierTypeForDecl(default_underlying_type_);
  }

  RegisterDecl(std::make_unique<Resource>(std::move(attributes), std::move(name),
                                          std::move(type_ctor), std::move(properties)));
}

void ConsumeStep::ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl) {
  auto name = Name::CreateSourced(library(), service_decl->identifier->span());
  auto context = NamingContext::Create(name);
  std::vector<Service::Member> members;
  for (auto& member : service_decl->members) {
    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(member->attributes), &attributes);

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), context->EnterMember(member->span()),
                                &type_ctor))
      return;
    members.emplace_back(std::move(type_ctor), member->identifier->span(), std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(service_decl->attributes), &attributes);

  RegisterDecl(
      std::make_unique<Service>(std::move(attributes), std::move(name), std::move(members)));
}

void ConsumeStep::MaybeOverrideName(AttributeList& attributes, NamingContext* context) {
  auto attr = attributes.Get("generated_name");
  if (attr == nullptr)
    return;

  CompileStep::CompileAttributeEarly(compiler(), attr);
  const auto* arg = attr->GetArg(AttributeArg::kDefaultAnonymousName);
  if (arg == nullptr || !arg->value->IsResolved()) {
    return;
  }
  const ConstantValue& value = arg->value->Value();
  ZX_ASSERT(value.kind == ConstantValue::Kind::kString);
  std::string str = static_cast<const StringConstantValue&>(value).MakeContents();
  if (utils::IsValidIdentifierComponent(str)) {
    context->set_name_override(std::move(str));
  } else {
    Fail(ErrInvalidGeneratedName, arg->span);
  }
}

// TODO(fxbug.dev/77853): these conversion methods may need to be refactored
//  once the new flat AST lands, and such coercion  is no longer needed.
template <typename T>
bool ConsumeStep::ConsumeValueLayout(std::unique_ptr<raw::Layout> layout,
                                     const std::shared_ptr<NamingContext>& context,
                                     std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                     Decl** out_decl) {
  std::vector<typename T::Member> members;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::ValueLayoutMember*>(mem.get());
    auto span = member->identifier->span();

    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(member->attributes), &attributes);

    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return false;

    members.emplace_back(span, std::move(value), std::move(attributes));
  }

  std::unique_ptr<TypeConstructor> subtype_ctor;
  if (layout->subtype_ctor != nullptr) {
    if (!ConsumeTypeConstructor(std::move(layout->subtype_ctor), context, &subtype_ctor))
      return false;
  } else {
    subtype_ctor = IdentifierTypeForDecl(default_underlying_type_);
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(raw_attribute_list), &attributes);
  MaybeOverrideName(*attributes, context.get());

  auto strictness = types::Strictness::kFlexible;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_strictness.has_value())
    strictness = layout->modifiers->maybe_strictness->value;

  if (layout->members.empty()) {
    if (strictness != types::Strictness::kFlexible)
      return Fail(ErrMustHaveOneMember, layout->span());
  }

  Decl* decl = RegisterDecl(
      std::make_unique<T>(std::move(attributes), context->ToName(library(), layout->span()),
                          std::move(subtype_ctor), std::move(members), strictness));
  if (out_decl) {
    *out_decl = decl;
  }
  return decl != nullptr;
}

template <typename T>
bool ConsumeStep::ConsumeOrdinaledLayout(std::unique_ptr<raw::Layout> layout,
                                         const std::shared_ptr<NamingContext>& context,
                                         std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                         Decl** out_decl) {
  std::vector<typename T::Member> members;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::OrdinaledLayoutMember*>(mem.get());
    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(member->attributes), &attributes);
    if (member->reserved) {
      members.emplace_back(ConsumeOrdinal(std::move(member->ordinal)), member->span(),
                           std::move(attributes));
      continue;
    }

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(member->type_ctor),
                                context->EnterMember(member->identifier->span()), &type_ctor))
      return false;

    members.emplace_back(ConsumeOrdinal(std::move(member->ordinal)), std::move(type_ctor),
                         member->identifier->span(), std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(raw_attribute_list), &attributes);
  MaybeOverrideName(*attributes, context.get());

  auto strictness = types::Strictness::kFlexible;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_strictness.has_value())
    strictness = layout->modifiers->maybe_strictness->value;

  auto resourceness = types::Resourceness::kValue;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_resourceness.has_value())
    resourceness = layout->modifiers->maybe_resourceness->value;

  Decl* decl = RegisterDecl(std::make_unique<T>(std::move(attributes),
                                                context->ToName(library(), layout->span()),
                                                std::move(members), strictness, resourceness));
  if (out_decl) {
    *out_decl = decl;
  }
  return decl != nullptr;
}

bool ConsumeStep::ConsumeStructLayout(std::unique_ptr<raw::Layout> layout,
                                      const std::shared_ptr<NamingContext>& context,
                                      std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                      Decl** out_decl) {
  std::vector<Struct::Member> members;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::StructLayoutMember*>(mem.get());

    std::unique_ptr<AttributeList> attributes;
    ConsumeAttributeList(std::move(member->attributes), &attributes);

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(member->type_ctor),
                                context->EnterMember(member->identifier->span()), &type_ctor))
      return false;

    std::unique_ptr<Constant> default_value;
    if (member->default_value != nullptr) {
      ConsumeConstant(std::move(member->default_value), &default_value);
    }

    Attribute* allow_struct_defaults = attributes->Get("allow_deprecated_struct_defaults");
    if (!allow_struct_defaults && default_value != nullptr) {
      Fail(ErrDeprecatedStructDefaults, mem->span());
    }

    members.emplace_back(std::move(type_ctor), member->identifier->span(), std::move(default_value),
                         std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(raw_attribute_list), &attributes);
  MaybeOverrideName(*attributes, context.get());

  auto resourceness = types::Resourceness::kValue;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_resourceness.has_value())
    resourceness = layout->modifiers->maybe_resourceness->value;

  Decl* decl = RegisterDecl(std::make_unique<Struct>(std::move(attributes),
                                                     context->ToName(library(), layout->span()),
                                                     std::move(members), resourceness));
  if (out_decl) {
    *out_decl = decl;
  }
  return decl != nullptr;
}

bool ConsumeStep::ConsumeLayout(std::unique_ptr<raw::Layout> layout,
                                const std::shared_ptr<NamingContext>& context,
                                std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                Decl** out_decl) {
  switch (layout->kind) {
    case raw::Layout::Kind::kBits: {
      return ConsumeValueLayout<Bits>(std::move(layout), context, std::move(raw_attribute_list),
                                      out_decl);
    }
    case raw::Layout::Kind::kEnum: {
      return ConsumeValueLayout<Enum>(std::move(layout), context, std::move(raw_attribute_list),
                                      out_decl);
    }
    case raw::Layout::Kind::kStruct: {
      return ConsumeStructLayout(std::move(layout), context, std::move(raw_attribute_list),
                                 out_decl);
    }
    case raw::Layout::Kind::kTable: {
      return ConsumeOrdinaledLayout<Table>(std::move(layout), context,
                                           std::move(raw_attribute_list), out_decl);
    }
    case raw::Layout::Kind::kUnion: {
      return ConsumeOrdinaledLayout<Union>(std::move(layout), context,
                                           std::move(raw_attribute_list), out_decl);
    }
  }
}

bool ConsumeStep::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                         const std::shared_ptr<NamingContext>& context,
                                         std::unique_ptr<raw::AttributeList> raw_attribute_list,
                                         std::unique_ptr<TypeConstructor>* out_type_ctor,
                                         Decl** out_inline_decl) {
  std::vector<std::unique_ptr<LayoutParameter>> params;
  std::optional<SourceSpan> params_span;

  if (raw_type_ctor->parameters) {
    params_span = raw_type_ctor->parameters->span();
    for (auto& p : raw_type_ctor->parameters->items) {
      auto param = std::move(p);
      auto span = param->span();
      switch (param->kind) {
        case raw::LayoutParameter::Kind::kLiteral: {
          auto literal_param = static_cast<raw::LiteralLayoutParameter*>(param.get());
          std::unique_ptr<LiteralConstant> constant;
          ConsumeLiteralConstant(literal_param->literal.get(), &constant);

          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<LiteralLayoutParameter>(std::move(constant), span);
          params.push_back(std::move(consumed));
          break;
        }
        case raw::LayoutParameter::Kind::kType: {
          auto type_param = static_cast<raw::TypeLayoutParameter*>(param.get());
          std::unique_ptr<TypeConstructor> type_ctor;
          if (!ConsumeTypeConstructor(std::move(type_param->type_ctor), context,
                                      /*raw_attribute_list=*/nullptr, &type_ctor,
                                      /*out_inline_decl=*/nullptr))
            return false;

          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<TypeLayoutParameter>(std::move(type_ctor), span);
          params.push_back(std::move(consumed));
          break;
        }
        case raw::LayoutParameter::Kind::kIdentifier: {
          auto id_param = static_cast<raw::IdentifierLayoutParameter*>(param.get());
          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<IdentifierLayoutParameter>(Reference(*id_param->identifier), span);
          params.push_back(std::move(consumed));
          break;
        }
      }
    }
  }

  std::vector<std::unique_ptr<Constant>> constraints;
  // TODO(fxbug.dev/87619): Here we fall back to the type ctor span to make
  // ErrProtocolConstraintRequired work. We should remove this.
  SourceSpan constraints_span = raw_type_ctor->layout_ref->span();

  if (raw_type_ctor->constraints) {
    constraints_span = raw_type_ctor->constraints->span();
    for (auto& c : raw_type_ctor->constraints->items) {
      std::unique_ptr<Constant> constraint;
      if (!ConsumeConstant(std::move(c), &constraint))
        return false;
      constraints.push_back(std::move(constraint));
    }
  }

  if (raw_type_ctor->layout_ref->kind == raw::LayoutReference::Kind::kInline) {
    auto inline_ref = static_cast<raw::InlineLayoutReference*>(raw_type_ctor->layout_ref.get());
    auto attributes = std::move(raw_attribute_list);
    if (inline_ref->attributes != nullptr)
      attributes = std::move(inline_ref->attributes);
    Decl* inline_decl;
    if (!ConsumeLayout(std::move(inline_ref->layout), context, std::move(attributes), &inline_decl))
      return false;

    if (out_inline_decl) {
      *out_inline_decl = inline_decl;
    }
    if (out_type_ctor) {
      *out_type_ctor = std::make_unique<TypeConstructor>(
          Reference(Reference::Target(inline_decl)),
          std::make_unique<LayoutParameterList>(std::move(params), params_span),
          std::make_unique<TypeConstraints>(std::move(constraints), constraints_span));
    }
    return true;
  }

  auto named_ref = static_cast<raw::NamedLayoutReference*>(raw_type_ctor->layout_ref.get());
  ZX_ASSERT_MSG(out_type_ctor, "out type ctors should always be provided for a named type ctor");
  *out_type_ctor = std::make_unique<TypeConstructor>(
      Reference(*named_ref->identifier),
      std::make_unique<LayoutParameterList>(std::move(params), params_span),
      std::make_unique<TypeConstraints>(std::move(constraints), constraints_span));
  return true;
}

bool ConsumeStep::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                         const std::shared_ptr<NamingContext>& context,
                                         std::unique_ptr<TypeConstructor>* out_type) {
  return ConsumeTypeConstructor(std::move(raw_type_ctor), context, /*raw_attribute_list=*/nullptr,
                                out_type, /*out_inline_decl=*/nullptr);
}

void ConsumeStep::ConsumeTypeDecl(std::unique_ptr<raw::TypeDecl> type_decl) {
  auto name = Name::CreateSourced(library(), type_decl->identifier->span());
  auto& layout_ref = type_decl->type_ctor->layout_ref;

  if (layout_ref->kind == raw::LayoutReference::Kind::kNamed) {
    if (experimental_flags().IsFlagEnabled(ExperimentalFlags::Flag::kAllowNewTypes)) {
      ConsumeNewType(std::move(type_decl));
      return;
    }
    auto named_ref = static_cast<raw::NamedLayoutReference*>(layout_ref.get());
    Fail(ErrNewTypesNotAllowed, type_decl->span(), name, named_ref->span().data());
    return;
  }

  ConsumeTypeConstructor(std::move(type_decl->type_ctor), NamingContext::Create(name),
                         std::move(type_decl->attributes),
                         /*out_type=*/nullptr, /*out_inline_decl=*/nullptr);
}

void ConsumeStep::ConsumeNewType(std::unique_ptr<raw::TypeDecl> type_decl) {
  ZX_ASSERT(type_decl->type_ctor->layout_ref->kind == raw::LayoutReference::Kind::kNamed);
  ZX_ASSERT(experimental_flags().IsFlagEnabled(ExperimentalFlags::Flag::kAllowNewTypes));

  std::unique_ptr<AttributeList> attributes;
  ConsumeAttributeList(std::move(type_decl->attributes), &attributes);

  auto new_type_name = Name::CreateSourced(library(), type_decl->identifier->span());

  std::unique_ptr<TypeConstructor> new_type_ctor;
  if (!ConsumeTypeConstructor(std::move(type_decl->type_ctor), NamingContext::Create(new_type_name),
                              &new_type_ctor))
    return;

  RegisterDecl(std::make_unique<NewType>(std::move(attributes), std::move(new_type_name),
                                         std::move(new_type_ctor)));
}

const raw::Literal* ConsumeStep::ConsumeLiteral(std::unique_ptr<raw::Literal> raw_literal) {
  auto ptr = raw_literal.get();
  library()->raw_literals.push_back(std::move(raw_literal));
  return ptr;
}

const raw::Identifier* ConsumeStep::ConsumeIdentifier(
    std::unique_ptr<raw::Identifier> raw_identifier) {
  auto ptr = raw_identifier.get();
  library()->raw_identifiers.push_back(std::move(raw_identifier));
  return ptr;
}

const raw::Ordinal64* ConsumeStep::ConsumeOrdinal(std::unique_ptr<raw::Ordinal64> raw_ordinal) {
  auto ptr = raw_ordinal.get();
  library()->raw_ordinals.push_back(std::move(raw_ordinal));
  return ptr;
}

}  // namespace fidl::flat

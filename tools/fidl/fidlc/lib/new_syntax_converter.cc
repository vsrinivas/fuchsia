// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The implementation for the ConvertingTreeVisitor that re-prints a raw::File
// back into text format per some set of syntax rules.
#include "fidl/new_syntax_converter.h"

namespace fidl::conv {

// Until FTP-033 is fully implemented, it is possible for "strict" types to not
// have an actual "strict" keyword preceding them (ie, "strict union U {...}"
// and "union U {...}" are represented identically in the raw AST).  This
// helper function works around that problem by determining whether or not the
// actual "strict" keyword was used in the declaration text.
std::optional<types::Strictness> optional_strictness(types::Strictness strictness, bool specified) {
  if (!specified) {
    return std::nullopt;
  }
  return strictness;
}

// For types that only accept the strictness modifier (currently "bits" and
// "enum"), we don't store the presence of the modifier keyword as a bool.
// Instead, we just match the first token to its sub-kind to deduce whether or
// not the modifier keyword is used.
std::optional<types::Strictness> optional_strictness(Token& decl_start_token) {
  switch (decl_start_token.subkind()) {
    case Token::Subkind::kStrict:
      return types::Strictness::kStrict;
    case Token::Subkind::kFlexible:
      return types::Strictness::kFlexible;
    default:
      return std::nullopt;
  }
}

// Returns the "builtin" definition underpinning a type.  If named declaration
// is actually an alias, this method will recurse until all aliases are
// dereferenced and an actual, FIDL-native type can be deduced.
std::optional<UnderlyingType> resolve_as_user_defined_type(const flat::Name& name,
                                                           bool is_behind_alias) {
  const flat::Library* lib = name.library();
  const flat::Decl* decl_ptr = lib->LookupDeclByName(name);
  if (decl_ptr == nullptr) {
    return std::nullopt;
  }

  const flat::Decl::Kind& kind = decl_ptr->kind;
  if (kind != flat::Decl::Kind::kTypeAlias) {
    if (kind == flat::Decl::Kind::kResource) {
      // Special case: the only "resource_definition" in existence at the
      // moment is the one that defines "handle," so if we get to this point, we
      // should just assume the underlying type is a handle.
      return UnderlyingType(flat::Type::Kind::kHandle, is_behind_alias);
    }
    return UnderlyingType(decl_ptr->kind, is_behind_alias);
  }

  auto type_alias_ptr = static_cast<const flat::TypeAlias*>(decl_ptr);
  auto underlying_type =
      resolve_as_user_defined_type(flat::GetName(type_alias_ptr->partial_type_ctor), true);
  if (underlying_type != std::nullopt) {
    return underlying_type;
  }
  return UnderlyingType(flat::GetType(type_alias_ptr->partial_type_ctor)->kind, true);
}

// Matches a string keyword to the "builtin" representing the FIDL-native type
// it represents.
std::optional<UnderlyingType> resolve_as_user_defined_type(const std::string& keyword) {
  const flat::Typespace root = flat::Typespace::RootTypes(nullptr);
  const flat::Name instrinsic = flat::Name::CreateIntrinsic(keyword);
  const flat::TypeTemplate* t = root.LookupTemplate(instrinsic, fidl::utils::Syntax::kOld);
  if (t == nullptr) {
    return std::nullopt;
  }

  if (keyword == "array") {
    return UnderlyingType(flat::Type::Kind::kArray, false);
  } else if (keyword == "vector") {
    return UnderlyingType(flat::Type::Kind::kVector, false);
  } else if (keyword == "bytes") {
    return UnderlyingType(flat::Type::Kind::kVector, false);
  } else if (keyword == "string") {
    return UnderlyingType(flat::Type::Kind::kString, false);
  } else if (keyword == "handle") {
    return UnderlyingType(flat::Type::Kind::kHandle, false);
  } else if (keyword == "request") {
    return UnderlyingType(flat::Type::Kind::kRequestHandle, false);
  } else {
    return UnderlyingType(flat::Type::Kind::kPrimitive, false);
  }
}

// Given a non-compound identifier, and a reference to the library in which
// that identifier is defined, we should be able to resolve the underlying
// built-in type underpinning that identifier.
std::optional<UnderlyingType> resolve_identifier(const std::unique_ptr<raw::Identifier>& identifier,
                                                 const flat::Library* lib) {
  std::string type_decl = identifier->copy_to_str();

  // Break up the type declaration - discard any "wrapped" types.
  size_t bracket_pos = type_decl.find_first_of('<');
  if (bracket_pos != std::string::npos) {
    type_decl = type_decl.substr(0, bracket_pos);
  };

  // We'll need to make a flat::Name from the type_decl string, which can then
  // be used to search the library for the name's definition recursively until
  // its underlying type can be deduced.
  auto underlying_type =
      resolve_as_user_defined_type(flat::Name::CreateSourced(lib, identifier->span()), false);
  if (underlying_type) {
    return underlying_type;
  }
  return resolve_as_user_defined_type(type_decl);
}

// Lookup the definition of a type's "key" identifier (ie, "vector" in the
// identifier "vector<array<uint8>:>" or "Foo" in "some.lib.Foo") in a given
// library.
std::optional<UnderlyingType> resolve_type(
    const std::unique_ptr<raw::TypeConstructorOld>& type_ctor, const flat::Library* lib) {
  std::unique_ptr<raw::CompoundIdentifier>& id = type_ctor->identifier;
  std::string type_decl = id->copy_to_str();

  // If there is at least one period in the declaration identifier, there is a
  // possibility that this is a reference to an imported library.  To verify
  // this, we'll construct the library name (ex, "some.library.Foo" becomes
  // just "some.library") and see if we can find it in the final library's
  // dependent libraries.
  size_t last_dot_pos = type_decl.find_last_of('.');
  if (last_dot_pos != std::string::npos) {
    flat::Library* dep_lib = nullptr;
    std::vector<std::string_view> lib_name;
    for (size_t i = 0; i < id->components.size() - 1; i++) {
      lib_name.emplace_back(id->components[i]->span().data());
    }

    auto filename = id->span().source_file().filename();
    if (lib->LookupDependency(filename, lib_name, &dep_lib)) {
      return resolve_identifier(id->components.back(), dep_lib);
    }
  };

  // Looks like this was not a reference to a definition in an imported
  // library after all.  Go ahead and look for it in our current library.
  return resolve_identifier(id->components.back(), lib);
}

std::optional<UnderlyingType> ConvertingTreeVisitor::resolve(
    const std::unique_ptr<raw::TypeConstructorOld>& type_ctor) {
  return resolve_type(type_ctor, library_);
}

void ConvertingTreeVisitor::OnAttributeOld(const raw::AttributeOld& element) {
  // This branching ensures that we do not attempt any conversion on doc comment
  // attributes.
  if (element.provenance == raw::AttributeOld::Provenance::kDefault) {
    std::optional<std::reference_wrapper<const raw::StringLiteral>> value = std::nullopt;
    if (element.value) {
      value = static_cast<const raw::StringLiteral&>(*element.value);
    }
    std::unique_ptr<Conversion> conv = std::make_unique<AttributeConversion>(element.name, value);
    Converting converting(this, std::move(conv), element.start_, element.end_);
  } else {
    std::unique_ptr<Conversion> conv =
        std::make_unique<NoopConversion>(element.start_, element.end_);
    Converting converting(this, std::move(conv), element.start_, element.end_);
  }
  TreeVisitor::OnAttributeOld(element);
}

void ConvertingTreeVisitor::OnAttributeListOld(
    const std::unique_ptr<raw::AttributeListOld>& element) {
  if (attribute_lists_seen_.insert(element.get()).second) {
    bool has_doc_comment = false;

    // If there is a "doc comment" attached to this attribute list, it will
    // always be the first element of that list.
    if (!element->attributes.empty() &&
        element->attributes[0].provenance == raw::AttributeOld::Provenance::kDocComment) {
      has_doc_comment = true;
    }

    std::unique_ptr<Conversion> conv = std::make_unique<AttributeListConversion>(has_doc_comment);
    Converting converting(this, std::move(conv), element->start_, element->end_);
    TreeVisitor::OnAttributeListOld(element);
  }
}

void ConvertingTreeVisitor::OnBitsDeclaration(
    const std::unique_ptr<raw::BitsDeclaration>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  Token& end = element->identifier->end_;
  if (element->maybe_type_ctor != nullptr) {
    end = element->maybe_type_ctor->end_;
  }

  auto ref =
      element->maybe_type_ctor == nullptr
          ? std::nullopt
          : std::make_optional<std::reference_wrapper<std::unique_ptr<raw::TypeConstructorOld>>>(
                element->maybe_type_ctor);
  std::unique_ptr<Conversion> conv = std::make_unique<BitsDeclarationConversion>(
      element->identifier, ref, optional_strictness(*element->decl_start_token));
  Converting converting(this, std::move(conv), *element->decl_start_token, end);
  TreeVisitor::OnBitsDeclaration(element);
}

void ConvertingTreeVisitor::OnBitsMember(const std::unique_ptr<raw::BitsMember>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv =
      std::make_unique<NoopConversion>(element->identifier->start_, element->value->end_);
  Converting converting(this, std::move(conv), element->identifier->start_, element->value->end_);
  TreeVisitor::OnBitsMember(element);
}

void ConvertingTreeVisitor::OnConstDeclaration(
    const std::unique_ptr<raw::ConstDeclaration>& element) {
  if (raw::IsAttributeListDefined(element->attributes)) {
    ConvertingTreeVisitor::OnAttributeList(element->attributes);
  }

  const auto& type_ctor = std::get<std::unique_ptr<raw::TypeConstructorOld>>(element->type_ctor);
  std::unique_ptr<Conversion> conv =
      std::make_unique<NameAndTypeConversion>(element->identifier, type_ctor);
  Converting converting(this, std::move(conv), type_ctor->start_, element->identifier->end_);
  TreeVisitor::OnConstDeclaration(element);
}

void ConvertingTreeVisitor::OnEnumDeclaration(
    const std::unique_ptr<raw::EnumDeclaration>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  Token& end = element->identifier->end_;
  if (element->maybe_type_ctor != nullptr) {
    end = element->maybe_type_ctor->end_;
  }

  auto ref =
      element->maybe_type_ctor == nullptr
          ? std::nullopt
          : std::make_optional<std::reference_wrapper<std::unique_ptr<raw::TypeConstructorOld>>>(
                element->maybe_type_ctor);
  std::unique_ptr<Conversion> conv = std::make_unique<EnumDeclarationConversion>(
      element->identifier, ref, optional_strictness(*element->decl_start_token));
  Converting converting(this, std::move(conv), *element->decl_start_token, end);
  TreeVisitor::OnEnumDeclaration(element);
}

void ConvertingTreeVisitor::OnEnumMember(const std::unique_ptr<raw::EnumMember>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv =
      std::make_unique<NoopConversion>(element->identifier->start_, element->value->end_);
  Converting converting(this, std::move(conv), element->identifier->start_, element->value->end_);
  TreeVisitor::OnEnumMember(element);
}

void ConvertingTreeVisitor::OnFile(std::unique_ptr<fidl::raw::File> const& element) {
  last_conversion_end_ = element->start_.previous_end().data().data();
  comments_ = std::move(element->comment_tokens_list);
  DeclarationOrderTreeVisitor::OnFile(element);
  converted_output_ += last_conversion_end_;
}

void ConvertingTreeVisitor::OnParameter(const std::unique_ptr<raw::Parameter>& element) {
  if (raw::IsAttributeListDefined(element->attributes)) {
    ConvertingTreeVisitor::OnAttributeListOld(
        std::get<std::unique_ptr<raw::AttributeListOld>>(element->attributes));
  }

  const auto& type_ctor = std::get<std::unique_ptr<raw::TypeConstructorOld>>(element->type_ctor);
  std::unique_ptr<Conversion> conv =
      std::make_unique<NameAndTypeConversion>(element->identifier, type_ctor);
  Converting converting(this, std::move(conv), type_ctor->start_, element->identifier->end_);
  TreeVisitor::OnParameter(element);
}

void ConvertingTreeVisitor::OnParameterListOld(
    const std::unique_ptr<raw::ParameterListOld>& element) {
  std::unique_ptr<Conversion> conv =
      std::make_unique<ParameterListConversion>(in_response_with_error_);
  Converting converting(this, std::move(conv), element->start_, element->end_);
  TreeVisitor::OnParameterListOld(element);
}

void ConvertingTreeVisitor::OnProtocolMethod(const std::unique_ptr<raw::ProtocolMethod>& element) {
  // This code should be functionally identical to that found in the original
  // TreeVisitor->OnProtocolMethod, except that it sets in_response_with_error_
  // before processing the potential response parameters list.
  if (raw::IsAttributeListDefined(element->attributes)) {
    OnAttributeList(element->attributes);
  }
  OnIdentifier(element->identifier);
  if (raw::IsParameterListDefined(element->maybe_request)) {
    OnParameterList(element->maybe_request);
  }
  in_response_with_error_ = raw::IsTypeConstructorDefined(element->maybe_error_ctor);
  if (raw::IsParameterListDefined(element->maybe_response)) {
    OnParameterList(element->maybe_response);
  }
  if (in_response_with_error_) {
    OnTypeConstructor(element->maybe_error_ctor);
  }
}

void ConvertingTreeVisitor::OnResourceProperty(
    const std::unique_ptr<raw::ResourceProperty>& element) {
  const auto& type_ctor = std::get<std::unique_ptr<raw::TypeConstructorOld>>(element->type_ctor);
  std::unique_ptr<Conversion> conv =
      std::make_unique<NameAndTypeConversion>(element->identifier, type_ctor);
  Converting converting(this, std::move(conv), type_ctor->start_, element->identifier->end_);
  TreeVisitor::OnResourceProperty(element);
}

void ConvertingTreeVisitor::OnServiceMember(const std::unique_ptr<raw::ServiceMember>& element) {
  if (raw::IsAttributeListDefined(element->attributes)) {
    ConvertingTreeVisitor::OnAttributeList(element->attributes);
  }

  const auto& type_ctor = std::get<std::unique_ptr<raw::TypeConstructorOld>>(element->type_ctor);
  std::unique_ptr<Conversion> conv =
      std::make_unique<NameAndTypeConversion>(element->identifier, type_ctor);
  Converting converting(this, std::move(conv), type_ctor->start_, element->identifier->end_);
  TreeVisitor::OnServiceMember(element);
}

void ConvertingTreeVisitor::OnStructDeclaration(
    const std::unique_ptr<raw::StructDeclaration>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv =
      std::make_unique<StructDeclarationConversion>(element->identifier, element->resourceness);
  Converting converting(this, std::move(conv), *element->decl_start_token,
                        element->identifier->end_);
  TreeVisitor::OnStructDeclaration(element);
}

void ConvertingTreeVisitor::OnStructMember(const std::unique_ptr<raw::StructMember>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv =
      std::make_unique<NameAndTypeConversion>(element->identifier, element->type_ctor);
  Converting converting(this, std::move(conv), element->type_ctor->start_,
                        element->identifier->end_);
  TreeVisitor::OnStructMember(element);
}

void ConvertingTreeVisitor::OnTableDeclaration(
    const std::unique_ptr<raw::TableDeclaration>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv =
      std::make_unique<TableDeclarationConversion>(element->identifier, element->resourceness);
  Converting converting(this, std::move(conv), *element->decl_start_token,
                        element->identifier->end_);
  TreeVisitor::OnTableDeclaration(element);
}

void ConvertingTreeVisitor::OnTableMember(const std::unique_ptr<raw::TableMember>& element) {
  if (element->maybe_used != nullptr) {
    if (element->maybe_used->attributes != nullptr) {
      ConvertingTreeVisitor::OnAttributeListOld(element->maybe_used->attributes);
    }

    std::unique_ptr<Conversion> conv = std::make_unique<NameAndTypeConversion>(
        element->maybe_used->identifier, element->maybe_used->type_ctor);
    Converting converting(this, std::move(conv), element->maybe_used->type_ctor->start_,
                          element->maybe_used->identifier->end_);
    TreeVisitor::OnTableMember(element);
  } else {
    TreeVisitor::OnTableMember(element);
  }
}

void ConvertingTreeVisitor::OnTypeConstructorOld(
    const std::unique_ptr<raw::TypeConstructorOld>& element) {
  std::optional<UnderlyingType> underlying_type = resolve(element);

  // We should never get a null Builtin - if we do, there is a mistake in the
  // converter code.  Failing this assert means we are looking at an
  // identifier that is neither explicitly defined in the source, nor
  // intrinsic to the language.  If that's the case, where did it come from?
  assert(underlying_type.has_value() && "must resolve underlying builtin value for type");

  std::unique_ptr<Conversion> conv =
      std::make_unique<TypeConversion>(element, underlying_type.value());
  Converting converting(this, std::move(conv), element->start_, element->end_);
  TreeVisitor::OnTypeConstructorOld(element);
}

void ConvertingTreeVisitor::OnUnionDeclaration(
    const std::unique_ptr<raw::UnionDeclaration>& element) {
  if (element->attributes != nullptr) {
    ConvertingTreeVisitor::OnAttributeListOld(element->attributes);
  }

  std::unique_ptr<Conversion> conv = std::make_unique<UnionDeclarationConversion>(
      element->identifier, optional_strictness(element->strictness, element->strictness_specified),
      element->resourceness);
  Converting converting(this, std::move(conv), *element->decl_start_token,
                        element->identifier->end_);
  TreeVisitor::OnUnionDeclaration(element);
}

void ConvertingTreeVisitor::OnUnionMember(const std::unique_ptr<raw::UnionMember>& element) {
  if (element->maybe_used != nullptr) {
    if (element->maybe_used->attributes != nullptr) {
      ConvertingTreeVisitor::OnAttributeListOld(element->maybe_used->attributes);
    }

    std::unique_ptr<Conversion> conv = std::make_unique<NameAndTypeConversion>(
        element->maybe_used->identifier, element->maybe_used->type_ctor);
    Converting converting(this, std::move(conv), element->maybe_used->type_ctor->start_,
                          element->maybe_used->identifier->end_);
    TreeVisitor::OnUnionMember(element);
  } else {
    TreeVisitor::OnUnionMember(element);
  }
}

void ConvertingTreeVisitor::OnUsing(const std::unique_ptr<raw::Using>& element) {
  TreeVisitor::OnUsing(element);
}

Converting::Converting(ConvertingTreeVisitor* ctv, std::unique_ptr<Conversion> conversion,
                       const Token& start, const Token& end)
    : ctv_(ctv) {
  const char* copy_from = ctv_->last_conversion_end_;
  const char* copy_until = start.data().data();
  const char* conversion_end = end.data().data() + end.data().length();

  if (conversion_end > ctv_->last_conversion_end_) {
    // We should only enter this block if we are in a nested conversion.
    ctv_->last_conversion_end_ = conversion_end;
  }
  if (copy_from < copy_until) {
    auto cr = std::make_unique<CopyRange>(copy_from, copy_until);
    conversion->AddPrefix(std::move(cr));
  }

  // Any stray comments contained inside the span being converted should be
  // added to the prefix as well.
  while (ctv->last_comment_ < ctv->comments_.size()) {
    std::string_view comment = ctv->comments_[ctv->last_comment_]->span().data();

    // Make sure not to consume comments past the end of the current conversion
    // span.
    if (comment.data() > ctv_->last_conversion_end_) {
      break;
    }

    if (comment.data() > start.data().data()) {
      const char* from = comment.data();
      const char* until = from + comment.length() + 1;
      auto cr = std::make_unique<CopyRange>(from, until);
      conversion->AddPrefix(std::move(cr));
    }
    ctv->last_comment_++;
  }

  ctv_->open_conversions_.push(std::move(conversion));
}

Converting::~Converting() {
  std::unique_ptr<Conversion> conv = std::move(ctv_->open_conversions_.top());
  ctv_->open_conversions_.pop();
  std::string text = conv->Write(ctv_->to_syntax_);
  if (!ctv_->open_conversions_.empty()) {
    ctv_->open_conversions_.top()->AddChildText(text);
  } else {
    ctv_->converted_output_ += text;
  }
}

}  // namespace fidl::conv

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/name.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/utils.h"

namespace fidl::flat {

// static
std::string NamingContext::BuildFlattenedName(SourceSpan name, Kind kind,
                                              const std::shared_ptr<NamingContext>& parent) {
  switch (kind) {
    case Kind::kDecl:
      return std::string(name.data());
    case Kind::kLayoutMember:
      return utils::to_upper_camel_case(std::string(name.data()));
    case Kind::kMethodRequest: {
      std::string result = utils::to_upper_camel_case(std::string(parent->name_.data()));
      result.append(utils::to_upper_camel_case(std::string(name.data())));
      result.append("Request");
      return result;
    }
    case Kind::kMethodResponse: {
      std::string result = utils::to_upper_camel_case(std::string(parent->name_.data()));
      result.append(utils::to_upper_camel_case(std::string(name.data())));
      result.append("Response");
      return result;
    }
    case Kind::kMethodResult: {
      std::string result = utils::to_upper_camel_case(std::string(parent->name_.data()));
      result.append(utils::to_upper_camel_case(std::string(name.data())));
      // We can't use [protocol][method]Response, because that is occupied by the success variant of
      // the result type, if this method has an error.
      //
      // TODO(fxbug.dev/95231): Unnecessary once we remove `TopResponse` wrappers altogether.
      result.append("TopResponse");
      return result;
    }
  }
}

std::shared_ptr<NamingContext> NamingContext::Create(const Name& decl_name) {
  ZX_ASSERT_MSG(decl_name.span().has_value(),
                "cannot have a naming context from a name without a span");
  return Create(decl_name.span().value());
}

std::vector<std::string> NamingContext::Context() const {
  std::vector<std::string> names;
  const auto* current = this;
  while (current) {
    // Internally, we don't store a separate context item to represent whether a
    // layout is the request or response, since this bit of information is
    // embedded in the Kind. When collapsing the stack of contexts into a list
    // of strings, we need to flatten this case out to avoid losing this data.
    switch (current->kind_) {
      case Kind::kMethodRequest: {
        names.push_back("Request");
        break;
      };
      case Kind::kMethodResponse:
      case Kind::kMethodResult: {
        names.push_back("Response");
        break;
      }
      case Kind::kDecl:
      case Kind::kLayoutMember:
        break;
    }

    names.emplace_back(current->name_.data());
    current = current->parent_.get();
  }
  std::reverse(names.begin(), names.end());
  return names;
}

Name NamingContext::ToName(Library* library, SourceSpan declaration_span) {
  if (parent_ == nullptr)
    return Name::CreateSourced(library, name_);
  return Name::CreateAnonymous(library, declaration_span, shared_from_this(),
                               Name::Provenance::kAnonymousLayout);
}

std::optional<SourceSpan> Name::span() const {
  return std::visit(
      [](auto&& name_context) -> std::optional<SourceSpan> {
        using T = std::decay_t<decltype(name_context)>;
        if constexpr (std::is_same_v<T, SourcedNameContext>) {
          return std::optional(name_context.span);
        } else if constexpr (std::is_same_v<T, AnonymousNameContext>) {
          return std::optional(name_context.span);
        } else if constexpr (std::is_same_v<T, IntrinsicNameContext>) {
          return std::nullopt;
        } else {
          abort();
        }
      },
      name_context_);
}

std::string_view Name::decl_name() const {
  return std::visit(
      [](auto&& name_context) -> std::string_view {
        using T = std::decay_t<decltype(name_context)>;
        if constexpr (std::is_same_v<T, SourcedNameContext>) {
          return name_context.span.data();
        } else if constexpr (std::is_same_v<T, AnonymousNameContext>) {
          // since decl_name() is used in Name::Key, using the flattened name
          // here ensures that the flattened name will cause conflicts if not
          // unique
          return std::string_view(name_context.flattened_name);
        } else if constexpr (std::is_same_v<T, IntrinsicNameContext>) {
          return std::string_view(name_context.name);
        } else {
          abort();
        }
      },
      name_context_);
}

std::string Name::full_name() const {
  auto name = std::string(decl_name());
  if (member_name_.has_value()) {
    constexpr std::string_view kSeparator = ".";
    name.reserve(name.size() + kSeparator.size() + member_name_.value().size());

    name.append(kSeparator);
    name.append(member_name_.value());
  }
  return name;
}

}  // namespace fidl::flat

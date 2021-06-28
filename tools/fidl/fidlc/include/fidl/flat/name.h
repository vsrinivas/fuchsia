// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "fidl/source_span.h"
#include "fidl/utils.h"

namespace fidl {
namespace flat {

class Name;
class Library;

// A NamingContext is a list of names, from least specific to most specific, which
// identifies the use of a layout. For example, for the FIDL:
//
// ```
// library fuchsia.bluetooth.le;
//
// protocol Peripheral {
//   StartAdvertising(table { 1: data struct {}; });
// };
// ```
//
// The context for the innermost empty struct can be built up by the calls:
//
//   auto ctx = NamingContext("Peripheral").FromRequest("StartAdvertising").EnterMember("data")
//
// `ctx` will produce a `FlattenedName` of "data", and a `Context` of
// ["Peripheral", "StartAdvertising", "data"].
class NamingContext : public std::enable_shared_from_this<NamingContext> {
 public:
  // Usage should only be through shared pointers, so that shared_from_this is always
  // valid. We use shared pointers to manage the lifetime of NamingContexts since the
  // parent pointers need to always be valid. Managing ownership with unique_ptr is tricky
  // (Push() would need to have access to a unique_ptr of this, and there would need to
  // be a place to own all the root nodes, which are not owned by an anonymous name), and
  // doing it manually is even worse.
  static std::shared_ptr<NamingContext> Create(SourceSpan decl_name) {
    return Create(decl_name, ElementKind::kDecl);
  }
  static std::shared_ptr<NamingContext> Create(const Name& decl_name);

  std::shared_ptr<NamingContext> EnterRequest(SourceSpan method_name) {
    assert(kind_ == ElementKind::kDecl && "request must follow protocol");
    return Push(method_name, ElementKind::kMethodRequest);
  }

  std::shared_ptr<NamingContext> EnterEvent(SourceSpan method_name) {
    assert(kind_ == ElementKind::kDecl && "event must follow protocol");
    // an event is actually a request from the server's perspective, so we use request in the
    // naming context
    return Push(method_name, ElementKind::kMethodRequest);
  }

  std::shared_ptr<NamingContext> EnterResponse(SourceSpan method_name) {
    assert(kind_ == ElementKind::kDecl && "response must follow protocol");
    return Push(method_name, ElementKind::kMethodResponse);
  }

  std::shared_ptr<NamingContext> EnterResult(SourceSpan span) {
    assert((kind_ == ElementKind::kMethodRequest || kind_ == ElementKind::kMethodResponse) &&
           "result must follow method");
    return Push(span, ElementKind::kMethodResult);
  }

  std::shared_ptr<NamingContext> EnterSuccessVariant(SourceSpan span) {
    assert(kind_ == ElementKind::kMethodResult && "success must follow result");
    return Push(span, ElementKind::kMethodSuccessVariant);
  }

  std::shared_ptr<NamingContext> EnterErrorVariant(SourceSpan span) {
    assert(kind_ == ElementKind::kMethodResult && "error must follow result");
    return Push(span, ElementKind::kMethodErrorVariant);
  }

  std::shared_ptr<NamingContext> EnterMember(SourceSpan member_name) {
    return Push(member_name, ElementKind::kLayoutMember);
  }

  SourceSpan name() const { return name_; }

  std::shared_ptr<NamingContext> parent() const {
    assert(parent_ != nullptr && "compiler bug: traversing above root");
    return parent_;
  }

  std::string FlattenedName() const {
    switch (kind_) {
      case ElementKind::kDecl:
        return std::string(name_.data());
      case ElementKind::kLayoutMember:
        return utils::to_upper_camel_case(std::string(name_.data()));
      case ElementKind::kMethodRequest: {
        std::string result = utils::to_upper_camel_case(std::string(parent()->name_.data()));
        result.append(utils::to_upper_camel_case(std::string(name_.data())));
        result.append("Request");
        return result;
      }
      case ElementKind::kMethodResponse: {
        std::string result = utils::to_upper_camel_case(std::string(parent()->name_.data()));
        result.append(utils::to_upper_camel_case(std::string(name_.data())));
        // We can't use [protocol][method]Response, because that may be occupied by
        // the success variant of the result type, if this method has an error.
        result.append("TopResponse");
        return result;
      }
      // The naming contexts associated with method results are handled specially
      // (see ElementKind)
      case ElementKind::kMethodResult: {
        auto method_name = parent()->name_.data();
        auto protocol_name = parent()->parent()->name_.data();
        return utils::StringJoin({protocol_name, method_name, "Result"}, "_");
      }
      case ElementKind::kMethodSuccessVariant: {
        auto method_name = parent()->parent()->name_.data();
        auto protocol_name = parent()->parent()->parent()->name_.data();
        return utils::StringJoin({protocol_name, method_name, "Response"}, "_");
      }
      case ElementKind::kMethodErrorVariant: {
        auto method_name = parent()->parent()->name_.data();
        auto protocol_name = parent()->parent()->parent()->name_.data();
        return utils::StringJoin({protocol_name, method_name, "Error"}, "_");
      }
    }
  }

  std::vector<std::string_view> Context() const {
    std::vector<std::string_view> names;
    const auto* current = this;
    while (current) {
      names.push_back(current->name_.data());
      current = current->parent_.get();
    }
    std::reverse(names.begin(), names.end());
    return names;
  }

  // ToName() exists to handle the case where the caller does not necessarily know what
  // kind of name (sourced or anonymous) this NamingContext corresponds to.
  // For example, this happens for layouts where the Consume* functions all take a
  // NamingContext and so the given layout may be at the top level of the library
  // (with a user-specified name) or may be nested/anonymous.
  Name ToName(Library* library, SourceSpan declaration_span);

 private:
  // Each new naming context is represented by a SourceSpan pointing to the name in
  // question (e.g. protocol/layout/member name), and an ElementKind. The contexts
  // are represented as linked lists with pointers back up to the parent to avoid
  // storing extraneous copies, thus the naming context for
  //
  //   type Foo = { member_a struct { ... }; member_b struct {...}; };
  //
  // Would look like
  //
  //   member_a --\
  //               ---> Foo
  //   member_b --/
  //
  // Note that there are additional constraints not captured in the type system:
  // for example, a kMethodRequest can only follow a kDecl, and a kDecl can only
  // appear as the "root" of a naming context. These are enforced somewhat loosely
  // using asserts in the class' public methods.

  enum class ElementKind {
    kDecl,
    kLayoutMember,
    kMethodRequest,
    kMethodResponse,
    // The naming scheme for the result type and the success variant in a response
    // with an error type predates the design of the anonymous name flattening
    // algorithm, and we therefore need to handle these names specially to ensure
    // backwards compatibility with the existing names (errors are handled specially
    // to be consistent with the rest). This requires distinguishing them as separate
    // cases in ElementKind, rather than just as ordinary kLayoutMembers
    kMethodResult,
    kMethodSuccessVariant,
    kMethodErrorVariant,
  };
  struct Element {
    SourceSpan name;
    ElementKind kind;
  };

  NamingContext(SourceSpan name, ElementKind kind) : name_(name), kind_(kind) {}

  static std::shared_ptr<NamingContext> Create(SourceSpan decl_name, ElementKind kind) {
    // We need to create a shared pointer but there are only private constructors. Since
    // we don't care about an extra allocation here, we use `new` to get around this
    // (see https://abseil.io/tips/134)
    return std::shared_ptr<NamingContext>(new NamingContext(decl_name, kind));
  }

  std::shared_ptr<NamingContext> Push(SourceSpan name, ElementKind kind) {
    auto ctx = Create(name, kind);
    ctx->parent_ = shared_from_this();
    return ctx;
  }

  SourceSpan name_;
  ElementKind kind_;
  std::shared_ptr<NamingContext> parent_ = nullptr;
};

// Name represents a named entry in a particular scope.

// Names have different flavors based on their origins, which can be determined by the discriminant
// `Name::Kind`. See the documentation for `Name::Kind` for details.
class Name final {
 public:
  // Helper type to use when looking up and comparing names. This may be useful for associative
  // containers.
  //
  // Note that this type contains `std::string_view`s, so it must not outlive the strings its
  // members reference.
  class Key final {
   public:
    // Intentionally allow for implicit conversions from `Name`, as `Name` should be freely
    // convertible to its `Key`.
    //
    // NOLINTNEXTLINE
    Key(const Name& name)
        : compare_context_(name.library(), name.decl_name(),
                           name.member_name().has_value()
                               ? std::make_optional(std::string_view(name.member_name().value()))
                               : std::nullopt) {}

    explicit Key(const Library* library, std::string_view decl_name)
        : compare_context_(library, decl_name, std::nullopt) {}

    explicit Key(const Library* library, std::string_view decl_name, std::string_view member_name)
        : compare_context_(library, decl_name, member_name) {}

    friend bool operator==(const Key& lhs, const Key& rhs) {
      return lhs.compare_context_ == rhs.compare_context_;
    }

    friend bool operator!=(const Key& lhs, const Key& rhs) {
      return lhs.compare_context_ != rhs.compare_context_;
    }

    friend bool operator<(const Key& lhs, const Key& rhs) {
      return lhs.compare_context_ < rhs.compare_context_;
    }

   private:
    using CompareContext =
        std::tuple<const Library*, std::string_view, std::optional<std::string_view>>;

    CompareContext compare_context_;
  };

  static Name CreateSourced(const Library* library, SourceSpan span) {
    return Name(library, SourcedNameContext(span), std::nullopt);
  }

  static Name CreateSourced(const Library* library, SourceSpan span, std::string member_name) {
    return Name(library, SourcedNameContext(span), member_name);
  }

  static Name CreateDerived(const Library* library, SourceSpan span, std::string name) {
    return Name(library, DerivedNameContext(std::move(name), span), std::nullopt);
  }

  static Name CreateAnonymous(const Library* library, SourceSpan span,
                              std::shared_ptr<NamingContext> context) {
    return Name(library, AnonymousNameContext(std::move(context), span), std::nullopt);
  }

  static Name CreateIntrinsic(std::string name) {
    return Name(nullptr, IntrinsicNameContext(std::move(name)), std::nullopt);
  }

  Name(const Name& other) noexcept
      : library_(other.library_),
        name_context_(other.name_context_),
        member_name_(other.member_name_) {}

  Name(Name&& other) noexcept
      : library_(other.library_),
        name_context_(std::move(other.name_context_)),
        member_name_(std::move(other.member_name_)) {
    other.library_ = nullptr;
    other.name_context_ = std::monostate();
  }

  Name& operator=(const Name& other) noexcept {
    if (&other == this) {
      return *this;
    }

    library_ = other.library_;
    name_context_ = other.name_context_;

    return *this;
  }

  Name& operator=(Name&& other) noexcept {
    if (&other == this) {
      return *this;
    }

    library_ = other.library_;
    other.library_ = nullptr;
    name_context_ = std::move(other.name_context_);
    other.name_context_ = std::monostate();

    return *this;
  }

  const Library* library() const { return library_; }

  std::optional<SourceSpan> span() const {
    return std::visit(
        [](auto&& name_context) -> std::optional<SourceSpan> {
          using T = std::decay_t<decltype(name_context)>;
          if constexpr (std::is_same_v<T, SourcedNameContext>) {
            return std::optional(name_context.span);
          } else if constexpr (std::is_same_v<T, DerivedNameContext>) {
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

  std::string_view decl_name() const {
    return std::visit(
        [](auto&& name_context) -> std::string_view {
          using T = std::decay_t<decltype(name_context)>;
          if constexpr (std::is_same_v<T, SourcedNameContext>) {
            return name_context.span.data();
          } else if constexpr (std::is_same_v<T, DerivedNameContext>) {
            return std::string_view(name_context.name);
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

  std::string full_name() const {
    auto name = std::string(decl_name());
    if (member_name_.has_value()) {
      constexpr std::string_view kSeparator = ".";
      name.reserve(name.size() + kSeparator.size() + member_name_.value().size());

      name.append(kSeparator);
      name.append(member_name_.value());
    }
    return name;
  }

  const std::optional<std::string>& member_name() const { return member_name_; }

  Key memberless_key() const { return Key(library_, decl_name()); }

  Key key() const { return Key(*this); }

  friend bool operator==(const Name& lhs, const Name& rhs) { return lhs.key() == rhs.key(); }

  friend bool operator!=(const Name& lhs, const Name& rhs) { return lhs.key() != rhs.key(); }

  friend bool operator<(const Name& lhs, const Name& rhs) { return lhs.key() < rhs.key(); }

 private:
  struct SourcedNameContext {
    explicit SourcedNameContext(SourceSpan span) : span(span) {}

    // The span of the name.
    SourceSpan span;
  };

  struct DerivedNameContext {
    explicit DerivedNameContext(std::string name, SourceSpan span)
        : name(std::move(name)), span(span) {}

    // The derived name.
    std::string name;

    // The span from which the name was derived.
    SourceSpan span;
  };

  struct AnonymousNameContext {
    explicit AnonymousNameContext(std::shared_ptr<NamingContext> context, SourceSpan span)
        : flattened_name(context->FlattenedName()), context(std::move(context)), span(span) {}

    std::string flattened_name;
    std::shared_ptr<NamingContext> context;
    // The span of the object to which this anonymous name refers to (anonymous names
    // by definition do not appear in source, so the name itself has no span).
    SourceSpan span;
  };

  struct IntrinsicNameContext {
    explicit IntrinsicNameContext(std::string name) : name(std::move(name)) {}

    // The intrinsic name.
    std::string name;
  };

  Name(const Library* library, SourcedNameContext name_context,
       std::optional<std::string> member_name)
      : library_(library),
        name_context_(std::move(name_context)),
        member_name_(std::move(member_name)) {}

  Name(const Library* library, DerivedNameContext name_context,
       std::optional<std::string> member_name)
      : library_(library),
        name_context_(std::move(name_context)),
        member_name_(std::move(member_name)) {}

  Name(const Library* library, AnonymousNameContext name_context,
       std::optional<std::string> member_name)
      : library_(library),
        name_context_(std::move(name_context)),
        member_name_(std::move(member_name)) {}

  Name(const Library* library, IntrinsicNameContext name_context,
       std::optional<std::string> member_name)
      : library_(library),
        name_context_(std::move(name_context)),
        member_name_(std::move(member_name)) {}

  const Library* library_;
  std::variant<std::monostate, SourcedNameContext, DerivedNameContext, AnonymousNameContext,
               IntrinsicNameContext>
      name_context_;
  std::optional<std::string> member_name_;

 public:
  bool is_sourced() const { return std::get_if<SourcedNameContext>(&name_context_); }
  const AnonymousNameContext* as_anonymous() const {
    return std::get_if<AnonymousNameContext>(&name_context_);
  }
};

}  // namespace flat
}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

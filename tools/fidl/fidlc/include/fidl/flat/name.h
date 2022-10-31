// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

#include <zircon/assert.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/source_span.h"

namespace fidl::flat {

class Name;
struct Library;

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
    return Create(decl_name, Kind::kDecl, nullptr);
  }
  static std::shared_ptr<NamingContext> Create(const Name& decl_name);

  std::shared_ptr<NamingContext> EnterRequest(SourceSpan method_name) {
    ZX_ASSERT_MSG(kind_ == Kind::kDecl, "request must follow protocol");
    return Push(method_name, Kind::kMethodRequest);
  }

  std::shared_ptr<NamingContext> EnterEvent(SourceSpan method_name) {
    ZX_ASSERT_MSG(kind_ == Kind::kDecl, "event must follow protocol");
    // an event is actually a request from the server's perspective, so we use request in the
    // naming context
    return Push(method_name, Kind::kMethodRequest);
  }

  std::shared_ptr<NamingContext> EnterResponse(SourceSpan method_name) {
    ZX_ASSERT_MSG(kind_ == Kind::kDecl, "response must follow protocol");
    return Push(method_name, Kind::kMethodResponse);
  }

  std::shared_ptr<NamingContext> EnterResult(SourceSpan method_name) {
    ZX_ASSERT_MSG(kind_ == Kind::kDecl, "result must follow protocol");
    return Push(method_name, Kind::kMethodResult);
  }

  std::shared_ptr<NamingContext> EnterMember(SourceSpan member_name) {
    return Push(member_name, Kind::kLayoutMember);
  }

  SourceSpan name() const { return name_; }

  std::shared_ptr<NamingContext> parent() const {
    ZX_ASSERT_MSG(parent_ != nullptr, "traversing above root");
    return parent_;
  }

  void set_name_override(std::string value) { flattened_name_override_ = std::move(value); }

  std::string_view flattened_name() const {
    if (flattened_name_override_) {
      return flattened_name_override_.value();
    }
    return flattened_name_;
  }
  std::vector<std::string> Context() const;

  // ToName() exists to handle the case where the caller does not necessarily know what
  // kind of name (sourced or anonymous) this NamingContext corresponds to.
  // For example, this happens for layouts where the Consume* functions all take a
  // NamingContext and so the given layout may be at the top level of the library
  // (with a user-specified name) or may be nested/anonymous.
  Name ToName(Library* library, SourceSpan declaration_span);

 private:
  // Each new naming context is represented by a SourceSpan pointing to the name
  // in question (e.g. protocol/layout/member name) and a Kind. The contexts are
  // represented as linked lists with pointers back up to the parent to avoid
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

  enum class Kind {
    kDecl,
    kLayoutMember,
    kMethodRequest,
    kMethodResponse,
    kMethodResult,
  };

  NamingContext(SourceSpan name, Kind kind, std::shared_ptr<NamingContext> parent)
      : name_(name),
        kind_(kind),
        parent_(std::move(parent)),
        flattened_name_(BuildFlattenedName(name_, kind_, parent_)) {}

  static std::string BuildFlattenedName(SourceSpan name, Kind kind,
                                        const std::shared_ptr<NamingContext>& parent);

  static std::shared_ptr<NamingContext> Create(SourceSpan decl_name, Kind kind,
                                               std::shared_ptr<NamingContext> parent) {
    // We need to create a shared pointer but there are only private constructors. Since
    // we don't care about an extra allocation here, we use `new` to get around this
    // (see https://abseil.io/tips/134)
    return std::shared_ptr<NamingContext>(new NamingContext(decl_name, kind, std::move(parent)));
  }

  std::shared_ptr<NamingContext> Push(SourceSpan name, Kind kind) {
    return Create(name, kind, shared_from_this());
  }

  SourceSpan name_;
  Kind kind_;
  std::shared_ptr<NamingContext> parent_;
  std::string flattened_name_;
  std::optional<std::string> flattened_name_override_;
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

  enum struct Provenance {
    // The name refers to an anonymous layout, like `struct {}`.
    kAnonymousLayout,
    // The name refers to a declaration that was generated by the compiler, like
    // the result union and the empty success struct generated for a method like
    // `Foo() -> () error uint32`.
    kCompilerGenerated,
  };

  static Name CreateAnonymous(const Library* library, SourceSpan span,
                              std::shared_ptr<NamingContext> context, Provenance provenance) {
    return Name(library, AnonymousNameContext(std::move(context), provenance, span), std::nullopt);
  }

  static Name CreateIntrinsic(const Library* library, std::string name) {
    return Name(library, IntrinsicNameContext(std::move(name)), std::nullopt);
  }

  Name(const Name& other) noexcept = default;

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

  Name WithMemberName(std::string member_name) const {
    ZX_ASSERT_MSG(!member_name_.has_value(), "already has a member name");
    Name new_name = *this;
    new_name.member_name_ = std::move(member_name);
    return new_name;
  }

  const Library* library() const { return library_; }

  std::optional<SourceSpan> span() const;
  std::string_view decl_name() const;
  std::string full_name() const;
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

  struct AnonymousNameContext {
    explicit AnonymousNameContext(std::shared_ptr<NamingContext> context, Provenance provenance,
                                  SourceSpan span)
        : flattened_name(context->flattened_name()),
          context(std::move(context)),
          provenance(provenance),
          span(span) {}

    // The string is owned by the naming context.
    std::string_view flattened_name;
    std::shared_ptr<NamingContext> context;
    Provenance provenance;
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
      : library_(library), name_context_(name_context), member_name_(std::move(member_name)) {}

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
  std::variant<std::monostate, SourcedNameContext, AnonymousNameContext, IntrinsicNameContext>
      name_context_;
  std::optional<std::string> member_name_;

 public:
  bool is_sourced() const { return std::get_if<SourcedNameContext>(&name_context_); }
  bool is_intrinsic() const { return std::get_if<IntrinsicNameContext>(&name_context_); }
  const AnonymousNameContext* as_anonymous() const {
    return std::get_if<AnonymousNameContext>(&name_context_);
  }
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

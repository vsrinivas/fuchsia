// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "fidl/source_span.h"

namespace fidl {
namespace flat {

class Library;

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

  Name(const Library* library, IntrinsicNameContext name_context,
       std::optional<std::string> member_name)
      : library_(library),
        name_context_(std::move(name_context)),
        member_name_(std::move(member_name)) {}

  const Library* library_;
  std::variant<std::monostate, SourcedNameContext, DerivedNameContext, IntrinsicNameContext>
      name_context_;
  std::optional<std::string> member_name_;

 public:
  bool is_sourced() const { return std::get_if<SourcedNameContext>(&name_context_); }
};

}  // namespace flat
}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_NAME_H_

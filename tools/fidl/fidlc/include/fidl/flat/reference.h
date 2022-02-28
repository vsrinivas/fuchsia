// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_

#include <optional>

#include "fidl/source_span.h"

namespace fidl::raw {
class CompoundIdentifier;
}

namespace fidl::flat {

struct Decl;
struct Element;
struct Library;
class Name;

// A reference refers to an element by name. For example, in `alias Foo = Bar;`,
// `Foo` is a name and `Bar` is a reference. References such as `Bar` get
// resolved in the ResolveStep. References can also be created pre-resolved with
// no corresponding source span, e.g. for the reference to an anonymous layout.
class Reference final {
 public:
  // Creates a reference from source.
  explicit Reference(const raw::CompoundIdentifier& name);
  // Creates a pre-resolved reference (currently only needed for decl targets).
  explicit Reference(Decl* target);

  // Returns the span of the reference, or null for pre-resolved references.
  std::optional<SourceSpan> span() const { return span_; }

  // Returns the string components that make up the reference. This is non-const
  // because it should only be used for resolving the reference.
  const std::vector<std::string_view>& components() {
    assert(!IsResolved() && "should not inspect components again after resolving");
    return components_;
  }

  // Resolves to target. If target is a member, must also provide maybe_parent.
  void Resolve(Element* target, Decl* maybe_parent = nullptr);
  bool IsResolved() const { return target_ != nullptr; }

  Element* target() const {
    assert(target_ && "reference is not resolved");
    return target_;
  }
  Name target_name() const;
  // Returns the library that the target element occurs in.
  const Library* target_library() const;
  // If resolved to a decl, returns it. If resolved to a member, returns its
  // parent decl.
  Decl* target_or_parent_decl() const;

 private:
  std::optional<SourceSpan> span_;
  std::vector<std::string_view> components_;
  Element* target_ = nullptr;
  Decl* maybe_parent_ = nullptr;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_

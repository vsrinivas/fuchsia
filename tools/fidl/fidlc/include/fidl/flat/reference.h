// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_

#include <optional>
#include <variant>

#include "tools/fidl/fidlc/include/fidl/source_span.h"

namespace fidl::raw {
class CompoundIdentifier;
}

namespace fidl::flat {

struct Decl;
struct Element;
struct Library;
class Name;

// A reference refers to an element by name, and is either sourced or synthetic.
// The difference between a name and a reference is that names are definitional,
// while references point to names. Some examples:
//
//     // `Foo` is a name, `Bar` is a sourced reference
//     alias Foo = Bar;
//
//     // `X` is a name, `some.lib.Y` is a sourced reference
//     const X = some.lib.Y;
//
//     // This enum contains a synthetic reference to the default underlying
//     // type, fidl.uint32.
//     type Fruit = enum { APPLE = 1; };
//
//     type Baz = struct {
//         // This struct member contains a synthetic reference to the anonymous
//         // layout named AnonTable.
//         anon_table table {};
//     };
//
class Reference final {
 public:
  // A target element (along with its parent decl, if it is a member).
  class Target {
   public:
    explicit Target(Decl* decl);
    explicit Target(Element* member, Decl* parent);

    Element* element() const { return target_; }
    Name name() const;
    // Returns the library that the element occurs in.
    const Library* library() const;
    // If element() is a decl, returns it. If it's a member, returns its parent.
    Decl* element_or_parent_decl() const;

   private:
    Element* target_;
    Decl* maybe_parent_ = nullptr;
  };

  // Creates a sourced reference.
  explicit Reference(const raw::CompoundIdentifier& name);
  // Creates a synthetic reference.
  explicit Reference(Target target);

  // Returns true if this is a synthetic reference.
  bool IsSynthetic() const { return !span_.has_value(); }

  // Returns the span of a sourced reference.
  SourceSpan span() const { return span_.value(); }

  enum class State {
    // Initial state of a sourced reference.
    kRawSourced,
    // Initial state of a synthetic reference.
    kRawSynthetic,
    // Intermediate state for all references.
    kKey,
    // Alternative intermediate state for sourced references.
    kContextual,
    // Final state for valid references.
    kResolved,
    // Final state for invalid references.
    kFailed,
  };

  // String components that make up a sourced reference.
  struct RawSourced {
    std::vector<std::string_view> components;
  };

  // The initial, pre-decomposition decl that a synthetic reference points to.
  // This is distinct from the final, post-decomposition resolved target.
  struct RawSynthetic {
    Target target;
  };

  // A key identifies a family of elements with a particular name. Unlike
  // RawSourced, the roles of each component have been decided, and the library
  // has been resolved. Unlike RawSynthetic, the key is stable across
  // decomposition, i.e. we can choose it before decomposing the AST, and then
  // use it for lookups after decomposing.
  struct Key {
   public:
    Key(const Library* library, std::string_view decl_name)
        : library(library), decl_name(decl_name) {}
    Key Member(std::string_view member_name) const { return Key(library, decl_name, member_name); }

    const Library* library;
    std::string_view decl_name;
    std::optional<std::string_view> member_name;

   private:
    Key(const Library* library, std::string_view decl_name, std::string_view member_name)
        : library(library), decl_name(decl_name), member_name(member_name) {}
  };

  // An alternative to Key for a single component whose meaning is contextual.
  // For example, in zx.handle:CHANNEL, CHANNEL is contextual and ultimately
  // resolves to zx.obj_type.CHANNEL.
  struct Contextual {
    std::string_view name;
  };

  State state() const;
  const RawSourced& raw_sourced() const { return std::get<RawSourced>(state_); }
  const RawSynthetic& raw_synthetic() const { return std::get<RawSynthetic>(state_); }
  const Key& key() const { return std::get<Key>(state_); }
  const Contextual& contextual() const { return std::get<Contextual>(state_); }
  const Target& resolved() const { return std::get<Target>(state_); }

  void SetKey(Key key);
  void MarkContextual();
  void ResolveTo(Target target);
  void MarkFailed();

 private:
  struct Failed {};

  std::optional<SourceSpan> span_;
  std::variant<RawSourced, RawSynthetic, Key, Contextual, Target, Failed> state_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_REFERENCE_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_

#include <string>
#include <utility>
#include <vector>

namespace zxdb {

// Identifiers can be explicitly global qualified ("::foo" in C++) or without
// global qualification ("foo" or "Foo::Bar" in C++). Note that relative
// can still include class or namespace qualifications.
enum class IdentifierQualification { kGlobal, kRelative };

// Base class for identifiers that have different types of components.
// Different languages might want to represent different aspects of an
// identifier. This class encapsulates the core hierarchical part of an
// identifier.
//
// Most code will want to use an "Identifier" which contains language-neutral
// string components.
//
// The ComponentType must be copyable and moveable and implement:
//  - Construction from simple name:
//      ComponentType(const std::string&)
//  - Conversion to a string:
//      std::string GetName(bool include_debug)
//  - Comparison:
//      operator==
//      operator!=
template <class ComponentType>
class IdentifierBase {
 public:
  using Qualification = IdentifierQualification;

  explicit IdentifierBase(Qualification qual = Qualification::kRelative)
      : qualification_(qual) {}

  // Makes an identifier from a single component. Without the qualification
  // means relative.
  explicit IdentifierBase(ComponentType comp)
      : qualification_(Qualification::kRelative),
        components_({std::move(comp)}) {}
  IdentifierBase(Qualification qual, ComponentType comp)
      : qualification_(qual), components_({std::move(comp)}) {}

  // Construction of a relative identifier from a simple single-name string.
  // This string is passed to the underlying component's constructor.
  IdentifierBase(const std::string& name)
      : qualification_(Qualification::kRelative),
        components_({ComponentType(name)}) {}

  // Makes an identifier over a range of components.
  template <class InputIterator>
  IdentifierBase(Qualification qual, InputIterator first, InputIterator last)
      : qualification_(qual), components_(first, last) {}

  bool operator==(const IdentifierBase<ComponentType>& other) const {
    return qualification_ == other.qualification_ &&
           components_ == other.components_;
  }
  bool operator!=(const IdentifierBase<ComponentType>& other) const {
    return !operator==(other);
  }

  std::vector<ComponentType>& components() { return components_; }
  const std::vector<ComponentType>& components() const { return components_; }

  bool empty() const {
    return components_.empty() && qualification_ == Qualification::kRelative;
  }

  // Appends a single component.
  void AppendComponent(ComponentType c) { components_.push_back(std::move(c)); }

  // Appends the components from the other identifier to this one.
  void Append(IdentifierBase<ComponentType> other) {
    for (auto& cur : other.components())
      components_.push_back(std::move(cur));
  }

  Qualification qualification() const { return qualification_; }

  // Returns a new identifier that's the scope of this one. The scope is
  // everything but the last component. The qualification remains unchanged.
  //
  // If there is only one component, the resulting identifier will be empty
  // (still leaving the qualification unchanged). Examples:
  //   "foo::bar<int>::baz"   -> "foo::bar<int>"
  //   "::foo::bar::baz" -> "::foo::bar"
  //   "foo"             -> ""
  //   ""                -> ""
  //   "::foo"           -> "::"
  //   "::"              -> "::"
  IdentifierBase<ComponentType> GetScope() const {
    if (components_.size() <= 1)
      return IdentifierBase<ComponentType>(qualification_);
    return IdentifierBase<ComponentType>(qualification_, components_.begin(),
                                         components_.end() - 1);
  }

  // Returns the full name with all components concatenated together.
  std::string GetFullName() const { return GetName(false); }

  // Returns a form for debugging where the parsing is more visible.
  std::string GetDebugName() const { return GetName(true); }

  // Returns the separator string for components. This is currently always "::"
  // but is exposed here as a getter to avoid hardcoding it everywhere and to
  // allow us to do language-specific separators in the future.
  const char* GetSeparator() const { return "::"; }

 private:
  // Backend for the name getters.
  std::string GetName(bool include_debug) const {
    std::string result;

    if (qualification_ == Qualification::kGlobal)
      result += GetSeparator();

    bool first = true;
    for (const auto& c : components_) {
      if (first) {
        first = false;
      } else {
        if (include_debug)
          result += "; ";
        result += GetSeparator();
      }

      result += c.GetName(include_debug);
    }
    return result;
  }

  Qualification qualification_ = Qualification::kRelative;

  std::vector<ComponentType> components_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_

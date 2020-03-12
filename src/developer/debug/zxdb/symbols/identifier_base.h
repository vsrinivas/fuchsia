// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zxdb {

// Identifiers can be explicitly global qualified ("::foo" in C++) or without
// global qualification ("foo" or "Foo::Bar" in C++). Note that relative
// can still include class or namespace qualifications.
enum class IdentifierQualification { kGlobal, kRelative };

enum class SpecialIdentifier {
  kNone = 0,  // Not special.

  // Used for "$(foo bar)" where there is no special name and the "$" is used to escape some
  // contents. This is not stored in an Identifier since the "contents" in this case is just an
  // identifier literal that can be stored normally. It is used by the parser to identify this case
  // of special identifier.
  kEscaped,

  kAnon,  // Anonymous namespace.
  kMain,  // Main function (uses DWARF-indicated "entrypoint" regardless of name).
  kPlt,   // PLT identifier.
  kRegister,

  kLast,  // Not a type, marker for array size.
};

// The input and output strings should includes the "$" but no parens, so kMain -> "$main" and kPlt
// -> "$plt". SpecialIdentifierToString returns the empty string for kNone and "$" for kEscaped.
// StringToSpecialIdentifier return kNone if there's no match.
std::string_view SpecialIdentifierToString(SpecialIdentifier i);
SpecialIdentifier StringToSpecialIdentifier(std::string_view name);

// Returns true if the given special identifier has data associated with it, e.g. "$plt(foo)".
// Returns false if there are no parens required. Returns true for kNone since in that case it's
// only the data.
bool SpecialIdentifierHasData(SpecialIdentifier i);

extern const char kAnonIdentifierComponentName[];

// Base class for identifiers that have different types of components. Different languages might
// want to represent different aspects of an identifier. This class encapsulates the core
// hierarchical part of an identifier.
//
// Code in the symbols directory must use "Identifier" which contains opaque strings as components.
// The "expr" library adds a "ParsedIdentifier" which has more C++-aware parsing of template types.
// See those classes for more.
//
// The ComponentType must be copyable and moveable and implement:
//  - Construction from simple name:
//      ComponentType(const std::string&)
//  - Conversion to a string:
//      std::string GetName(bool include_debug)
//  - Comparison:
//      operator==
//      operator!=
//
// TODO(brettw) there is currently an annoying amount of duplicating between Identifier[Base] and
// ParsedIdentifier, and also a fair bit of converting back-and-forth. We should consider moving
// ParsedIdentifier to symbols and using that everywhere (renamed to Identifier), and having a
// "parse" callback function for the higher-level "expr" layer to inject its full parser into the
// lower-layer symbol directory.
template <class ComponentType>
class IdentifierBase {
 public:
  using Qualification = IdentifierQualification;

  explicit IdentifierBase(Qualification qual = Qualification::kRelative) : qualification_(qual) {}

  // Makes an identifier from a single component. Without the qualification means relative.
  explicit IdentifierBase(ComponentType comp)
      : qualification_(Qualification::kRelative), components_({std::move(comp)}) {}
  IdentifierBase(Qualification qual, ComponentType comp)
      : qualification_(qual), components_({std::move(comp)}) {}

  // Construction of a relative identifier from a simple single-name string. This string is passed
  // to the underlying component's constructor.
  IdentifierBase(const std::string& name)
      : qualification_(Qualification::kRelative), components_({ComponentType(name)}) {}

  // Makes an identifier over a range of components.
  template <class InputIterator>
  IdentifierBase(Qualification qual, InputIterator first, InputIterator last)
      : qualification_(qual), components_(first, last) {}

  // Comparisons. "==" compares everything for exact equality, "EqualsIgnoringQualification"
  // checks that everything is equal except the global/relative qualification flag.
  bool EqualsIgnoringQualification(const IdentifierBase<ComponentType>& other) const {
    return components_ == other.components_;
  }
  bool operator==(const IdentifierBase<ComponentType>& other) const {
    return qualification_ == other.qualification_ && EqualsIgnoringQualification(other);
  }
  bool operator!=(const IdentifierBase<ComponentType>& other) const { return !operator==(other); }

  std::vector<ComponentType>& components() { return components_; }
  const std::vector<ComponentType>& components() const { return components_; }

  bool empty() const { return components_.empty() && qualification_ == Qualification::kRelative; }

  // Appends a single component.
  void AppendComponent(ComponentType c) { components_.push_back(std::move(c)); }

  // Appends the components from the other identifier to this one.
  void Append(IdentifierBase<ComponentType> other) {
    for (auto& cur : other.components())
      components_.push_back(std::move(cur));
  }

  Qualification qualification() const { return qualification_; }
  void set_qualification(Qualification q) { qualification_ = q; }

  // Returns a new identifier that's the scope of this one. The scope is everything but the last
  // component. The qualification remains unchanged.
  //
  // If there is only one component, the resulting identifier will be empty (still leaving the
  // qualification unchanged). Examples:
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

  // Returns the full name with all components concatenated together, including or omitting the
  // global qualifier (leading "::"), if any.
  std::string GetFullName() const { return GetName(true, false); }
  std::string GetFullNameNoQual() const { return GetName(false, false); }

  // Returns a form for debugging where the parsing is more visible.
  std::string GetDebugName() const { return GetName(true, true); }

  // Returns the separator string for components. This is currently always "::" but is exposed here
  // as a getter to avoid hardcoding it everywhere and to allow us to do language-specific
  // separators in the future.
  const char* GetSeparator() const { return "::"; }

 private:
  // Backend for the name getters.
  //
  // A leading "::" will be included for globally qualified identifiers only when
  // include_global_qual is set.
  std::string GetName(bool include_global_qual, bool include_debug) const {
    std::string result;

    if (include_global_qual && qualification_ == Qualification::kGlobal)
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

      std::string component_name = c.GetName(include_debug);
      if (component_name.empty())
        result += kAnonIdentifierComponentName;
      else
        result += component_name;
    }
    return result;
  }

  Qualification qualification_ = Qualification::kRelative;

  std::vector<ComponentType> components_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_BASE_H_

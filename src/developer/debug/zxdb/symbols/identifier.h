// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class Err;

// An identifier is a sequence of names. Currently this handles C++ and Rust,
// but could be enhanced in the future for other languages.
//
// This is used for variable names and function names. If you type a class
// name or a typedef, the parser will also parse it as an identifier. What
// the identifier actually means will depend on the context in which it's used.
//
// One component can consist of a name and a template part (note currently the
// parser doesn't support the template part, but this class does in expectation
// that parsing support will be added in the future).
//
//   Component := [ "::" ] <Name> [ "<" <Template-Goop> ">" ]
//
// An identifier consists of one or more components. In C++, if the first
// component has a valid separator token, it's fully qualified ("::foo"), but
// it could be omitted for non-fully-qualified names. Subsequent components
// will always have separators.
//
// The identifier contains the token information for the original so that
// it can be used for syntax highlighting.
class Identifier {
 public:
  class Component {
   public:
    Component();

    // Constructor for names without templates.
    explicit Component(std::string name)
        : name_(std::move(name)), has_template_(false) {}

    // Constructor for names with templates. The contents will be a
    // vector of somewhat-normalized type string in between the <>. This always
    // generates a template even if the contents are empty (meaning "name<>");
    Component(std::string name, std::vector<std::string> template_contents)
        : name_(std::move(name)),
          has_template_(true),
          template_contents_(std::move(template_contents)) {}

    bool has_template() const { return has_template_; }

    const std::string& name() const { return name_; }
    // void set_name(std::string n) { name_ = std::move(n); }

    const std::vector<std::string>& template_contents() const {
      return template_contents_;
    }

    // Returns this component, either as a string as it would be represented in
    // C++, or in our debug format for unit test format checking (the name and
    // each template parameter will be separately quoted so we can check the
    // parsing).
    std::string GetName(bool include_debug) const;

   private:
    std::string name_;

    bool has_template_;

    std::vector<std::string> template_contents_;
  };

  // Identifiers can be explicitly global qualified ("::foo" in C++) or without
  // global qualification ("foo" or "Foo::Bar" in C++). Note that relative
  // can still include class or namespace qualifications.
  enum Qualification { kGlobal, kRelative };

  explicit Identifier(Qualification qual = kRelative) : qualification_(qual) {}

  // Makes a simple identifier with a standalone name. Without the
  // qualification means relative.
  explicit Identifier(std::string name);
  Identifier(Qualification qual, std::string name);

  // Makes an identifier from a single component. Without the
  // qualification means relative.
  explicit Identifier(Component comp);
  Identifier(Qualification qual, Component comp);

  // Makes an identifier over a range of components.
  template <class InputIterator>
  Identifier(Qualification qual, InputIterator first, InputIterator last)
      : qualification_(qual), components_(first, last) {}

  std::vector<Component>& components() { return components_; }
  const std::vector<Component>& components() const { return components_; }

  bool empty() const {
    return components_.empty() && qualification_ == kRelative;
  }

  void AppendComponent(Component c);
  void AppendComponent(std::string name);
  void AppendComponent(std::string name,
                       std::vector<std::string> template_contents);

  // Appends the components from the other identifier to this one.
  void Append(Identifier other);

  Qualification qualification() const { return qualification_; }

  // Returns a new identifier that's the scope of this one. The scope is
  // everything but the last component. The qualification remains unchanged.
  //
  // If there is only one component, the resulting identifier will be empty
  // (still leaving the qualification unchanged). Examples:
  //   "foo::bar::baz"   -> "foo::bar"
  //   "::foo::bar::baz" -> "::foo::bar"
  //   "foo"             -> ""
  //   ""                -> ""
  //   "::foo"           -> "::"
  //   "::"              -> "::"
  Identifier GetScope() const;

  // Returns the full name with all components concatenated together.
  std::string GetFullName() const;

  // Returns a form for debugging where the parsing is more visible.
  std::string GetDebugName() const;

  // Returns the separator string for components. This is currently always "::"
  // but is exposed here as a getter to avoid hardcoding it everywhere and to
  // allow us to do language-specific separators in the future.
  const char* GetSeparator() const;

  // In many contexts (like function parameters and local variables) the name
  // can't have any :: or template parameters and can have only one component.
  // If this identifier satisfies this requirement, a pointer to the single
  // string is returned. If there is zero or more than one component or any
  // template specs, returns null.
  //
  // The returned pointer will be invalidated if the Identifier is mutated.
  const std::string* GetSingleComponentName() const;

 private:
  // Backend for the name getters.
  std::string GetName(bool include_debug) const;

  Qualification qualification_ = kRelative;

  std::vector<Component> components_;
};

}  // namespace zxdb

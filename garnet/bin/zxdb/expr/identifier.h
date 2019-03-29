// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "garnet/bin/zxdb/expr/expr_token.h"
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
    Component(ExprToken separator, ExprToken name)
        : separator_(std::move(separator)), name_(std::move(name)) {}

    // Constructor for names without templates for use by tests that hard-code
    // values.
    Component(bool has_separator, const std::string& name)
        : name_(ExprTokenType::kName, name, 0) {
      if (has_separator)
        separator_ = ExprToken(ExprTokenType::kColonColon, "::", 0);
    }

    // Constructor for names with templates. The contents will be a
    // vector of somewhat-normalized type string in between the <>.
    Component(ExprToken separator, ExprToken name, ExprToken template_begin,
              std::vector<std::string> template_contents,
              ExprToken template_end)
        : separator_(std::move(separator)),
          name_(std::move(name)),
          template_begin_(std::move(template_begin)),
          template_contents_(std::move(template_contents)),
          template_end_(std::move(template_end)) {}

    bool has_separator() const {
      return separator_.type() != ExprTokenType::kInvalid;
    }
    bool has_template() const {
      return template_begin_.type() != ExprTokenType::kInvalid;
    }

    const ExprToken& separator() const { return separator_; }
    void set_separator(ExprToken t) { separator_ = std::move(t); }

    const ExprToken& name() const { return name_; }
    void set_name(ExprToken n) { name_ = std::move(n); }

    // This will be kInvalid if there is no template on this component.
    // The begin and end are the <> tokens, and the contents is the normalized
    // string in between. Note that the contents may not exactly match the
    // input string (some whitespace may be removed).
    const ExprToken& template_begin() const { return template_begin_; }
    const std::vector<std::string>& template_contents() const {
      return template_contents_;
    }
    const ExprToken& template_end() const { return template_end_; }

    // Returns this component, either as a string as it would be represented in
    // C++, or in our debug format for unit test format checking (the name and
    // each template parameter will be separately quoted so we can check the
    // parsing).
    //
    // include_separator controls whether the "::" is included in the result or
    // not.
    std::string GetName(bool include_debug, bool include_separator) const;

   private:
    ExprToken separator_;
    ExprToken name_;

    ExprToken template_begin_;
    std::vector<std::string> template_contents_;
    ExprToken template_end_;
  };

  Identifier() = default;

  // Makes a simple identifier with a standalone name.
  explicit Identifier(ExprToken name);

  // Makes an identifier from a single component.
  explicit Identifier(Component comp);

  // Attempts to parse the given string as an identifier. Returns either a
  // set Err or the resulting Identifier when the Err is not set.
  static std::pair<Err, Identifier> FromString(const std::string& input);

  // Makes an identifier over a range of components.
  template <class InputIterator>
  Identifier(InputIterator first, InputIterator last)
      : components_(first, last) {}

  std::vector<Component>& components() { return components_; }
  const std::vector<Component>& components() const { return components_; }

  void AppendComponent(Component c);
  void AppendComponent(ExprToken separator, ExprToken name);
  void AppendComponent(ExprToken separator, ExprToken name,
                       ExprToken template_begin,
                       std::vector<std::string> template_contents,
                       ExprToken template_end);

  // Appends the components from the other identifier to this one.
  void Append(Identifier other);

  // Returns a new identifier that's the scope of this one. The scope is
  // everything but the last identifier.
  //
  // If there is only one component, the resulting identifier will either be
  // empty (if the component has no separator, e.g. "Foo" becomes ""), or
  // contain only a separator (if the component has a separator, e.g. "::Foo"
  // becomes "::" and "::" becomes itself).
  Identifier GetScope() const;

  // Returns true if this identifier begins with "::" and as such can only be
  // resolved in the global namespace.
  bool InGlobalNamespace() const;

  // Returns the full name with all components concatenated together.
  std::string GetFullName() const;

  // Returns a form for debugging where the parsing is more visible.
  std::string GetDebugName() const;

  // Returns the list of components, each with their template parameters
  // converted to a string. For example:
  //
  //   { "std", "vector<std::string>" }
  //
  // This is the format used in the ModuleSymbolIndex for lookup.
  std::vector<std::string> GetAsIndexComponents() const;

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

  std::vector<Component> components_;
};

}  // namespace zxdb

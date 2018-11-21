// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/expr/expr_token.h"

namespace zxdb {

// An identifier is a sequence of names. Currently this handles C++ and Rust,
// but could be enhanced in the future for other languages.
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
    Component(ExprToken separator, ExprToken name, ExprToken template_spec)
        : separator_(std::move(separator)),
          name_(std::move(name)),
          template_spec_(std::move(template_spec)) {}

    bool has_separator() const {
      return separator_.type() != ExprToken::kInvalid;
    }
    bool has_template_spec() const {
      return template_spec_.type() != ExprToken::kInvalid;
    }

    const ExprToken& separator() const { return separator_; }
    void set_separator(ExprToken t) { separator_ = std::move(t); }

    const ExprToken& name() const { return name_; }

    const ExprToken& template_spec() const { return template_spec_; }

   private:
    ExprToken separator_;
    ExprToken name_;
    ExprToken template_spec_;  // Includes the < > on each end.
  };

  Identifier() = default;

  // Makes a simple identifier with a standalone name.
  Identifier(ExprToken name);

  std::vector<Component>& components() { return components_; }
  const std::vector<Component>& components() const { return components_; }

  void AppendComponent(Component c);
  void AppendComponent(ExprToken separator, ExprToken name,
                       ExprToken template_spec);

  // Returns the full name with all components concatenated together.
  std::string GetFullName() const;

  // Returns a form for debugging where the parsing is more visible.
  std::string GetDebugName() const;

 private:
  std::vector<Component> components_;
};

}  // namespace zxdb

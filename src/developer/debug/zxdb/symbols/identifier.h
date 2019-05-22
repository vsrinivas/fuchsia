// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_

#include <string>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/symbols/identifier_base.h"

namespace zxdb {

class IdentifierComponent {
 public:
  IdentifierComponent();

  // Constructor for names without templates.
  explicit IdentifierComponent(std::string name)
      : name_(std::move(name)), has_template_(false) {}

  // Constructor for names with templates. The contents will be a
  // vector of somewhat-normalized type string in between the <>. This always
  // generates a template even if the contents are empty (meaning "name<>");
  IdentifierComponent(std::string name,
                      std::vector<std::string> template_contents)
      : name_(std::move(name)),
        has_template_(true),
        template_contents_(std::move(template_contents)) {}

  bool operator==(const IdentifierComponent& other) const {
    return name_ == other.name_ && has_template_ == other.has_template_ &&
           template_contents_ == other.template_contents_;
  }
  bool operator!=(const IdentifierComponent& other) const {
    return !operator==(other);
  }

  bool has_template() const { return has_template_; }

  const std::string& name() const { return name_; }

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
using Identifier = IdentifierBase<IdentifierComponent>;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_IDENTIFIER_H_

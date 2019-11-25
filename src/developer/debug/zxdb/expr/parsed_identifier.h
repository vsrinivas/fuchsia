// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSED_IDENTIFIER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSED_IDENTIFIER_H_

#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

// Component for a fully parsed identifier component. Unlike the regular IdentifierComponent, this
// includes parsed template information. It may be extended in the future to support different
// languages (while the base Identifier will always only support opaque string components).
class ParsedIdentifierComponent {
 public:
  ParsedIdentifierComponent();

  // Constructor for names without templates.
  explicit ParsedIdentifierComponent(std::string name)
      : name_(std::move(name)), has_template_(false) {}

  // Constructor for names with templates. The contents will be a vector of somewhat-normalized type
  // string in between the <>. This always generates a template even if the contents are empty
  // (meaning "name<>");
  ParsedIdentifierComponent(std::string name, std::vector<std::string> template_contents)
      : name_(std::move(name)),
        has_template_(true),
        template_contents_(std::move(template_contents)) {}

  bool operator==(const ParsedIdentifierComponent& other) const {
    return name_ == other.name_ && has_template_ == other.has_template_ &&
           template_contents_ == other.template_contents_;
  }
  bool operator!=(const ParsedIdentifierComponent& other) const { return !operator==(other); }

  bool has_template() const { return has_template_; }

  const std::string& name() const { return name_; }

  const std::vector<std::string>& template_contents() const { return template_contents_; }

  // Returns this component, either as a string as it would be represented in C++, or in our debug
  // format for unit test format checking (the name and each template parameter will be separately
  // quoted so we can check the parsing).
  std::string GetName(bool include_debug) const;

 private:
  std::string name_;

  bool has_template_;

  std::vector<std::string> template_contents_;
};

// An identifier that includes components with template types parsed out. This is different then
// "Identifier" in the symbols directory because we attempt to actually parse and canonicalize the
// input according to language-specific rules.
using ParsedIdentifier = IdentifierBase<ParsedIdentifierComponent>;

// Converts a ParsedIdentifier to an Identifier and vice-versa. Conversion to a ParsedIdentifier may
// fail in which case the raw text of each component will be placed into the "name" part.
Identifier ToIdentifier(const ParsedIdentifier& parsed);
ParsedIdentifier ToParsedIdentifier(const Identifier& ident);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PARSED_IDENTIFIER_H_

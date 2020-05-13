// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_

#include <set>
#include <vector>

#include "raw_ast.h"
#include "reporter.h"

namespace fidl {

using reporter::Reporter;

class AttributesBuilder {
 public:
  AttributesBuilder(Reporter* reporter) : reporter_(reporter) {}

  AttributesBuilder(Reporter* reporter, std::vector<raw::Attribute> attributes)
      : reporter_(reporter), attributes_(std::move(attributes)) {
    for (auto& attribute : attributes_) {
      names_.emplace(attribute.name);
    }
  }

  bool Insert(raw::Attribute attribute);
  std::vector<raw::Attribute> Done();

 private:
  struct InsertResult {
    enum Kind {
      kOk,
      kDuplicate,
    };

    InsertResult(Kind kind, std::string message_fragment)
        : kind(kind), message_fragment(message_fragment) {}

    Kind kind;
    std::string message_fragment;
  };

  InsertResult InsertHelper(raw::Attribute attribute);

  Reporter* reporter_;
  std::vector<raw::Attribute> attributes_;
  std::set<std::string> names_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_

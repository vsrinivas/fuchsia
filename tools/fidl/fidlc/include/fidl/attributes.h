// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ATTRIBUTES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ATTRIBUTES_H_

#include <iostream>
#include <set>
#include <vector>

#include "raw_ast.h"
#include "reporter.h"
#include "utils.h"

namespace fidl {

using reporter::Reporter;

template <typename T>
class AttributesBuilder {
 public:
  AttributesBuilder(Reporter* reporter) : reporter_(reporter) {}

  AttributesBuilder(Reporter* reporter, std::vector<std::unique_ptr<T>> attributes)
      : reporter_(reporter), attributes_(std::move(attributes)) {
    for (auto& attribute : attributes_) {
      names_.emplace(utils::canonicalize(attribute->name));
    }
  }

  bool Insert(std::unique_ptr<T> attribute) {
    auto attribute_name = utils::canonicalize(attribute->name);
    auto attribute_span = attribute->span();
    auto result = InsertHelper(std::move(attribute));
    switch (result.kind) {
      case InsertResult::Kind::kDuplicate: {
        reporter_->Report(diagnostics::ErrDuplicateAttribute, attribute_span, attribute_name);
        return false;
      }
      case InsertResult::Kind::kOk:
        return true;
    }  // switch
  }

  std::vector<std::unique_ptr<T>> Done() { return std::move(attributes_); }

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

  InsertResult InsertHelper(std::unique_ptr<T> attribute) {
    if (!names_.emplace(utils::canonicalize(attribute->name)).second) {
      return InsertResult(InsertResult::kDuplicate, "");
    }
    attributes_.push_back(std::move(attribute));
    return InsertResult(InsertResult::kOk, "");
  }

  Reporter* reporter_;
  std::vector<std::unique_ptr<T>> attributes_;
  std::set<std::string> names_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_ATTRIBUTES_H_

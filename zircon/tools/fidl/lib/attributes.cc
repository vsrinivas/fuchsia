// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/attributes.h"

namespace fidl {

using namespace diagnostics;

bool AttributesBuilder::Insert(raw::Attribute attribute) {
  auto attribute_name = attribute.name;
  auto attribute_span = attribute.span();
  auto result = InsertHelper(std::move(attribute));
  switch (result.kind) {
    case InsertResult::Kind::kDuplicate: {
      reporter_->Report(ErrDuplicateAttribute, attribute_span, attribute_name);
      return false;
    }
    case InsertResult::Kind::kOk:
      return true;
  }  // switch
}

std::vector<raw::Attribute> AttributesBuilder::Done() { return std::move(attributes_); }

AttributesBuilder::InsertResult AttributesBuilder::InsertHelper(raw::Attribute attribute) {
  if (!names_.emplace(attribute.name).second) {
    return InsertResult(InsertResult::kDuplicate, "");
  }
  auto attribute_name = attribute.name;
  auto attribute_value = attribute.value;
  attributes_.push_back(std::move(attribute));
  return InsertResult(InsertResult::kOk, "");
}

}  // namespace fidl

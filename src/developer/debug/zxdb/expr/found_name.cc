// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/found_name.h"

#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

FoundName::FoundName() = default;

FoundName::FoundName(Kind kind, ParsedIdentifier name) : kind_(kind), name_(std::move(name)) {
  // These are the only kinds that don't require other information.
  FXL_DCHECK(kind == kNone || kind == kNamespace || kind == kTemplate);
}

FoundName::FoundName(const Variable* variable) : kind_(kVariable), variable_(RefPtrTo(variable)) {}

FoundName::FoundName(const Function* function) : kind_(kFunction), function_(RefPtrTo(function)) {}

FoundName::FoundName(const Variable* object_ptr, FoundMember member)
    : kind_(kMemberVariable), object_ptr_(RefPtrTo(object_ptr)), member_(std::move(member)) {}

FoundName::FoundName(const Variable* object_ptr, InheritancePath path,
                     const DataMember* data_member)
    : kind_(kMemberVariable),
      object_ptr_(RefPtrTo(object_ptr)),
      member_(std::move(path), data_member) {}

FoundName::FoundName(fxl::RefPtr<Type> type) : kind_(kType), type_(std::move(type)) {}

FoundName::~FoundName() = default;

ParsedIdentifier FoundName::GetName() const {
  switch (kind_) {
    case kNone:
      break;
    case kVariable:
      return ToParsedIdentifier(variable_->GetIdentifier());
    case kMemberVariable:
      return ToParsedIdentifier(member_.data_member()->GetIdentifier());
    case kNamespace:
    case kTemplate:
      return name_;
    case kType:
      return ToParsedIdentifier(type_->GetIdentifier());
    case kFunction:
      return ToParsedIdentifier(function_->GetIdentifier());
  }
  return ParsedIdentifier();
}

}  // namespace zxdb

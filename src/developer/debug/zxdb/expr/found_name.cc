// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/found_name.h"

#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FoundName::FoundName() = default;

FoundName::FoundName(Kind kind) : kind_(kind) {
  // These are the only kinds that don't require other information.
  FXL_DCHECK(kind == kNone || kind == kNamespace || kind == kTemplate);
}

FoundName::FoundName(const Variable* variable)
    : kind_(kVariable), variable_(const_cast<Variable*>(variable)) {}

FoundName::FoundName(const Variable* object_ptr, FoundMember member)
    : kind_(kMemberVariable),
      object_ptr_(const_cast<Variable*>(object_ptr)),
      member_(std::move(member)) {}

FoundName::FoundName(const Variable* object_ptr, const DataMember* data_member,
                     uint32_t data_member_offset)
    : kind_(kMemberVariable),
      object_ptr_(const_cast<Variable*>(object_ptr)),
      member_(data_member, data_member_offset) {}

FoundName::FoundName(fxl::RefPtr<Type> type)
    : kind_(kType), type_(std::move(type)) {}

FoundName::~FoundName() = default;

}  // namespace zxdb

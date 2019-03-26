// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/found_name.h"

#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/variable.h"

namespace zxdb {

FoundName::FoundName(const Variable* variable)
    : variable_(const_cast<Variable*>(variable)) {}

FoundName::FoundName(const Variable* object_ptr, FoundMember member)
    : object_ptr_(const_cast<Variable*>(object_ptr)),
      member_(std::move(member)) {}

FoundName::FoundName(const Variable* object_ptr, const DataMember* data_member,
                     uint32_t data_member_offset)
    : object_ptr_(const_cast<Variable*>(object_ptr)),
      member_(data_member, data_member_offset) {}

FoundName::~FoundName() = default;

}  // namespace zxdb

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/found_member.h"

namespace zxdb {

FoundMember::FoundMember() = default;

FoundMember::FoundMember(const DataMember* data_member,
                         uint32_t data_member_offset)
    : data_member_(const_cast<DataMember*>(data_member)),
      data_member_offset_(data_member_offset) {}

FoundMember::~FoundMember() = default;

}  // namespace zxdb

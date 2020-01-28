// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/found_member.h"

namespace zxdb {

FoundMember::FoundMember() = default;

FoundMember::FoundMember(const Collection* collection, const DataMember* data_member)
    : object_path_(RefPtrTo(collection)), data_member_(RefPtrTo(data_member)) {}

FoundMember::FoundMember(InheritancePath path, const DataMember* data_member)
    : object_path_(std::move(path)), data_member_(RefPtrTo(data_member)) {}

FoundMember::~FoundMember() = default;

std::optional<uint32_t> FoundMember::GetDataMemberOffset() const {
  if (is_null())
    return std::nullopt;

  auto containing_offset = object_path_.BaseOffsetInDerived();
  if (!containing_offset)
    return std::nullopt;  // Virtual inheritance.

  if (data_member_->is_external())
    return std::nullopt;  // Static.

  return *containing_offset + data_member_->member_location();
}

}  // namespace zxdb

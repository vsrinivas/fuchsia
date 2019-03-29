// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/data_member.h"

namespace zxdb {

DataMember::DataMember() : Value(DwarfTag::kMember) {}
DataMember::~DataMember() = default;

const DataMember* DataMember::AsDataMember() const { return this; }

}  // namespace zxdb

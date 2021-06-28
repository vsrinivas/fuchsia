// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/tagged_data_builder.h"

#include <lib/syslog/cpp/macros.h>

namespace zxdb {

void TaggedDataBuilder::Append(containers::array_view<uint8_t> new_data) {
  FX_DCHECK(data_.size() == tags_.size());
  data_.insert(data_.end(), new_data.begin(), new_data.end());
  tags_.insert(tags_.end(), new_data.size(), TaggedData::kValid);
}

void TaggedDataBuilder::AppendUnknown(size_t count) {
  FX_DCHECK(data_.size() == tags_.size());
  data_.insert(data_.end(), count, 0);
  tags_.insert(tags_.end(), count, TaggedData::kUnknown);
}

// Destructively returns a TaggedData, resetting the builder to empty.
TaggedData TaggedDataBuilder::TakeData() {
  FX_DCHECK(data_.size() == tags_.size());
  return TaggedData(std::move(data_), std::move(tags_));
}

}  // namespace zxdb

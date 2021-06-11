// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/const_value.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

namespace zxdb {

ConstValue::ConstValue(int64_t v) {
  data_.resize(sizeof(int64_t));
  memcpy(data_.data(), &v, sizeof(int64_t));
}

ConstValue::ConstValue(std::vector<uint8_t> buffer) : data_(std::move(buffer)) {}

std::vector<uint8_t> ConstValue::GetConstValue(size_t byte_count) const {
  FX_DCHECK(has_value());

  std::vector<uint8_t> result;
  if (byte_count && data_.size()) {
    result.resize(byte_count);
    memcpy(result.data(), data_.data(), std::min(byte_count, data_.size()));
  }
  return result;
}

}  // namespace zxdb

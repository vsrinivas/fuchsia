// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/lib/convert.h"

#include <string.h>

namespace benchmark {

std::string ToString(fidl::Array<uint8_t>& data) {
  std::string ret;
  ret.resize(data.size());
  memcpy(&ret[0], data.data(), data.size());
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

}  // namespace benchmark

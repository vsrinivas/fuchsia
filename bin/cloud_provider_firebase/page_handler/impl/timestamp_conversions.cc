// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/impl/timestamp_conversions.h"

#include <lib/fxl/logging.h>

namespace cloud_provider_firebase {

namespace {

const char kTimestampVersion[] = "01";
const size_t kVersionSize = 2;
const size_t kTimestampSize = 8;

}  // namespace

std::string ServerTimestampToBytes(int64_t timestamp) {
  static_assert(sizeof(kTimestampVersion) == kVersionSize + 1,
                "Wrong version size");
  static_assert(sizeof(timestamp) == kTimestampSize, "Wrong timestamp size");
  std::string bytes;
  bytes.reserve(kVersionSize + kTimestampSize);
  bytes.append(kTimestampVersion);
  bytes.append(
      std::string(reinterpret_cast<char*>(&timestamp), sizeof(timestamp)));
  return bytes;
}

int64_t BytesToServerTimestamp(const std::string& bytes) {
  if (bytes.substr(0u, kVersionSize) == kTimestampVersion) {
    return *reinterpret_cast<const int64_t*>(bytes.data() + kVersionSize);
  }
  FXL_NOTREACHED();
  return 0;
}

}  // namespace cloud_provider_firebase

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/testing/cpp/fidl.h>

#include "src/ledger/lib/encoding/encoding.h"
#include "src/ledger/lib/vmo/vector.h"

namespace cloud_provider {
namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::vector<uint8_t> serialized_commits(data, data + size);
  fuchsia::mem::Buffer buffer;
  if (!ledger::VmoFromVector(serialized_commits, &buffer)) {
    return 1;
  }
  fuchsia::ledger::testing::TestStruct commits;
  ledger::DecodeFromBuffer(buffer, &commits);
  return 0;
}

}  // namespace
}  // namespace cloud_provider

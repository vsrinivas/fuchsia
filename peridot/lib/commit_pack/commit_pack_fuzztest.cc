// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/vector.h>

#include "peridot/lib/commit_pack/commit_pack.h"

namespace cloud_provider {
namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::vector<uint8_t> serialized_commits(data, data + size);
  fuchsia::mem::Buffer buffer;
  if (!fsl::VmoFromVector(serialized_commits, &buffer)) {
    return 1;
  }
  CommitPack commit_pack = {std::move(buffer)};
  std::vector<CommitPackEntry> commits;
  DecodeCommitPack(commit_pack, &commits);
  return 0;
}

}  // namespace
}  // namespace cloud_provider

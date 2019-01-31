// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COMMIT_PACK_COMMIT_PACK_H_
#define PERIDOT_LIB_COMMIT_PACK_COMMIT_PACK_H_

#include <string>
#include <vector>

#include <fuchsia/ledger/cloud/cpp/fidl.h>

namespace cloud_provider {

using CommitPack = fuchsia::ledger::cloud::CommitPack;

// Represents a single commit to be encoded in the commit pack.
struct CommitPackEntry {
  std::string id;
  std::string data;
};

bool operator==(const CommitPackEntry& lhs, const CommitPackEntry& rhs);

bool EncodeCommitPack(std::vector<CommitPackEntry> commits,
                      CommitPack* commit_pack);

bool DecodeCommitPack(const CommitPack& commit_pack,
                      std::vector<CommitPackEntry>* commits);

}  // namespace cloud_provider

#endif  // PERIDOT_LIB_COMMIT_PACK_COMMIT_PACK_H_

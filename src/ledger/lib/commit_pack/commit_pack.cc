// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/commit_pack/commit_pack.h"

#include <fuchsia/ledger/cloud/c/fidl.h>
#include <lib/fidl/cpp/encoder.h>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/lib/fsl/vmo/vector.h"

namespace cloud_provider {

bool operator==(const CommitPackEntry& lhs, const CommitPackEntry& rhs) {
  return lhs.id == rhs.id && lhs.data == rhs.data;
}

bool EncodeCommitPack(std::vector<CommitPackEntry> commits, CommitPack* commit_pack) {
  FXL_DCHECK(commit_pack);

  fuchsia::ledger::cloud::Commits serialized_commits;
  for (auto& commit : commits) {
    serialized_commits.commits.push_back(std::move(fuchsia::ledger::cloud::Commit()
                                                       .set_id(convert::ToArray(commit.id))
                                                       .set_data(convert::ToArray(commit.data))));
  }

  return ledger::EncodeToBuffer(&serialized_commits, &commit_pack->buffer);
}

bool DecodeCommitPack(const CommitPack& commit_pack, std::vector<CommitPackEntry>* commits) {
  FXL_DCHECK(commits);
  commits->clear();

  fuchsia::ledger::cloud::Commits result;
  if (!ledger::DecodeFromBuffer(commit_pack.buffer, &result)) {
    return false;
  }

  for (auto& commit : result.commits) {
    if (!commit.has_id() || !commit.has_data()) {
      return false;
    }
    commits->emplace_back();
    commits->back().id = convert::ToString(commit.id());
    commits->back().data = convert::ToString(commit.data());
  }
  return true;
}

}  // namespace cloud_provider

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/commit_pack/commit_pack.h"

#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/convert/convert.h"
#include "peridot/public/fidl/fuchsia.ledger.cloud/serialized_commits_generated.h"

namespace cloud_provider {

bool operator==(const CommitPackEntry& lhs, const CommitPackEntry& rhs) {
  return lhs.id == rhs.id && lhs.data == rhs.data;
}

bool EncodeCommitPack(std::vector<CommitPackEntry> commits,
                      CommitPack* commit_pack) {
  FXL_DCHECK(commit_pack);
  flatbuffers::FlatBufferBuilder builder;

  auto entries_offsets = builder.CreateVector(
      commits.size(),
      static_cast<std::function<flatbuffers::Offset<SerializedCommit>(size_t)>>(
          [&builder, &commits](size_t i) {
            const auto& entry = commits[i];
            return CreateSerializedCommit(
                builder, convert::ToFlatBufferVector(&builder, entry.id),
                convert::ToFlatBufferVector(&builder, entry.data));
          }));

  builder.Finish(CreateSerializedCommits(builder, entries_offsets));
  return fsl::VmoFromString(convert::ToStringView(builder),
                            &commit_pack->buffer);
}

bool DecodeCommitPack(const CommitPack& commit_pack,
                      std::vector<CommitPackEntry>* commits) {
  FXL_DCHECK(commits);
  std::string data;
  if (!fsl::StringFromVmo(commit_pack.buffer, &data)) {
    return false;
  }

  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifySerializedCommitsBuffer(verifier)) {
    return false;
  }

  const SerializedCommits* serialized_commits =
      GetSerializedCommits(reinterpret_cast<const unsigned char*>(data.data()));

  std::vector<CommitPackEntry> result;
  result.reserve(serialized_commits->commits()->size());

  for (const auto* serialized_commit : *(serialized_commits->commits())) {
    result.push_back({convert::ToString(serialized_commit->id()),
                      convert::ToString(serialized_commit->data())});
  }

  commits->swap(result);
  return true;
}

}  // namespace cloud_provider

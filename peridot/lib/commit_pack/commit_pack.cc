// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/commit_pack/commit_pack.h"

#include <fuchsia/ledger/cloud/c/fidl.h>
#include <lib/fidl/cpp/encoder.h>
#include <lib/fsl/vmo/vector.h>

#include "peridot/lib/convert/convert.h"

using fuchsia::ledger::cloud::SerializedCommit;
using fuchsia::ledger::cloud::SerializedCommits;

namespace cloud_provider {

bool operator==(const CommitPackEntry& lhs, const CommitPackEntry& rhs) {
  return lhs.id == rhs.id && lhs.data == rhs.data;
}

bool EncodeCommitPack(std::vector<CommitPackEntry> commits,
                      CommitPack* commit_pack) {
  FXL_DCHECK(commit_pack);

  SerializedCommits serialized_commits;
  for (auto& commit : commits) {
    serialized_commits.commits.push_back(SerializedCommit{
        convert::ToArray(commit.id), convert::ToArray(commit.data)});
  }

  // Serialization of the SerializedCommits in a buffer. In this particular
  // case, we do not rely on FIDL encoding being stable: the buffer is simply a
  // way to extend the maximum message size, and we expect the buffer to be
  // immediately deserialized.
  // Caveats: see https://fuchsia-review.googlesource.com/c/fuchsia/+/262309
  // TODO(ambre): rewrite this using a more-supported API when available.
  fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
  // We need to preallocate the size of the structure in the encoder, the rest
  // is allocated when the vector is encoded.
  encoder.Alloc(sizeof(fuchsia_ledger_cloud_SerializedCommits));
  fidl::Encode(&encoder, &serialized_commits, 0);
  return fsl::VmoFromVector(encoder.TakeBytes(), &commit_pack->buffer);
}

bool DecodeCommitPack(const CommitPack& commit_pack,
                      std::vector<CommitPackEntry>* commits) {
  FXL_DCHECK(commits);
  commits->clear();

  std::vector<uint8_t> data;
  if (!fsl::VectorFromVmo(commit_pack.buffer, &data)) {
    return false;
  }

  // Deserialization of the SerializedCommits. See the comment in
  // EncodeCommitPack for details/caveats.
  // TODO(ambre): rewrite this using a supported API when available.
  fidl::Message message(fidl::BytePart(data.data(), data.size(), data.size()),
                        fidl::HandlePart());
  const char* error_msg;
  zx_status_t status = message.Decode(SerializedCommits::FidlType, &error_msg);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Decoding invalid CommitPack: " << error_msg;
    return false;
  }

  fidl::Decoder decoder(std::move(message));
  SerializedCommits result;
  SerializedCommits::Decode(&decoder, &result, 0);
  for (auto& commit : result.commits) {
    commits->emplace_back();
    commits->back().id = convert::ToString(commit.id);
    commits->back().data = convert::ToString(commit.data);
  }
  return true;
}

}  // namespace cloud_provider

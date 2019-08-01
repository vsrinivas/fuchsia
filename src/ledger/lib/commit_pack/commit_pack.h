// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_COMMIT_PACK_COMMIT_PACK_H_
#define SRC_LEDGER_LIB_COMMIT_PACK_COMMIT_PACK_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/encoder.h>
#include <lib/fsl/vmo/vector.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace cloud_provider {

using CommitPack = fuchsia::ledger::cloud::CommitPack;

// Represents a single commit to be encoded in the commit pack.
struct CommitPackEntry {
  std::string id;
  std::string data;
};

bool operator==(const CommitPackEntry& lhs, const CommitPackEntry& rhs);

bool EncodeCommitPack(std::vector<CommitPackEntry> commits, CommitPack* commit_pack);

bool DecodeCommitPack(const CommitPack& commit_pack, std::vector<CommitPackEntry>* commits);

// Serializes T in a buffer. In our use case, we do not rely on FIDL encoding
// being stable: the buffer is simply a way to extend the maximum message size,
// and we expect the buffer to be immediately deserialized.
// Caveats: see https://fuchsia-review.googlesource.com/c/fuchsia/+/262309
// TODO(ambre): rewrite this using a more-supported API when available.
template <typename T>
bool EncodeToBuffer(T* data, fuchsia::mem::Buffer* buffer) {
  FXL_DCHECK(data);
  FXL_DCHECK(buffer);

  fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
  // We need to preallocate the size of the structure in the encoder, the rest
  // is allocated when the vector is encoded.
  encoder.Alloc(fidl::CodingTraits<T>::encoded_size);
  fidl::Encode(&encoder, data, 0);
  return fsl::VmoFromVector(encoder.TakeBytes(), buffer);
}

// Deserialization of T from a buffer. See the comment in EncodeToBuffer for
// details/caveats.
// TODO(ambre): rewrite this using a supported API when available.
template <typename T>
bool DecodeFromBuffer(const fuchsia::mem::Buffer& buffer, T* data) {
  FXL_DCHECK(data);

  std::vector<uint8_t> bytes;
  if (!fsl::VectorFromVmo(buffer, &bytes)) {
    return false;
  }

  fidl::Message message(fidl::BytePart(bytes.data(), bytes.size(), bytes.size()),
                        fidl::HandlePart());
  const char* error_msg;
  zx_status_t status = message.Decode(T::FidlType, &error_msg);
  if (status != ZX_OK) {
    return false;
  }

  fidl::Decoder decoder(std::move(message));
  T::Decode(&decoder, data, 0);
  return true;
}

}  // namespace cloud_provider

#endif  // SRC_LEDGER_LIB_COMMIT_PACK_COMMIT_PACK_H_

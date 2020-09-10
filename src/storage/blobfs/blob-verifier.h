// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_
#define SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/macros.h>

#include "blob-corruption-notifier.h"
#include "metrics.h"

namespace blobfs {

// BlobVerifier verifies the contents of a blob against a merkle tree.
class BlobVerifier {
 public:
  // Creates an instance of BlobVerifier for blobs named |digest|, using the provided merkle
  // tree which is at most |merkle_size| bytes.
  //
  // The passed-in BlobfsMetrics will be updated when this class runs. The pointer must outlive this
  // class.
  //
  // Returns an error if the merkle tree's root does not match |digest|, or if the required tree
  // size for |data_size| bytes is bigger than |merkle_size|.
  [[nodiscard]] static zx_status_t Create(digest::Digest digest, BlobfsMetrics* metrics,
                                          const void* merkle, size_t merkle_size, size_t data_size,
                                          const BlobCorruptionNotifier* notifier,
                                          std::unique_ptr<BlobVerifier>* out);

  // Creates an instance of BlobVerifier for blobs named |digest|, which are small enough to not
  // have a stored merkle tree (i.e. MerkleTreeBytes(data_size) == 0).
  //
  // The passed-in BlobfsMetrics will be updated when this class runs. The pointer must outlive this
  // class.
  [[nodiscard]] static zx_status_t CreateWithoutTree(digest::Digest digest, BlobfsMetrics* metrics,
                                                     size_t data_size,
                                                     const BlobCorruptionNotifier* notifier,
                                                     std::unique_ptr<BlobVerifier>* out);

  // Verifies the entire contents of a blob. |buffer_size| is the total size of the buffer and the
  // buffer must be zeroed from |data_size| to |buffer_size|.
  // TODO(45457): Make const if MerkleTreeVerifier::Verify becomes const
  [[nodiscard]] zx_status_t Verify(const void* data, size_t data_size, size_t buffer_size);

  // Verifies a range of the contents of a blob from [data_offset, data_offset + length).
  // IMPORTANT: |data| is expected to be a pointer to the blob's contents at |data_offset|, not the
  // absolute start of the blob's data. (This facilitates partial verification when the blob is only
  // partially mapped in.) |buffer_size| is the total size of the buffer (relative to |data|) and
  // the buffer must be zerored from |data_size| to |buffer_size|.
  // TODO(45457): Make const if MerkleTreeVerifier::Verify becomes const
  [[nodiscard]] zx_status_t VerifyPartial(const void* data, size_t length, size_t data_offset,
                                          size_t buffer_size);

  // Modifies |data_off| and |buf_len| to be aligned to the minimum number of merkle tree nodes that
  // covered their original range.
  [[nodiscard]] zx_status_t Align(size_t* data_off, size_t* buf_len) const {
    return tree_verifier_.Align(data_off, buf_len);
  }

  size_t GetTreeLength() const { return tree_verifier_.GetTreeLength(); }

  const Digest& digest() { return digest_; }

 private:
  // Use |Create| or |CreateWithoutTree| to construct.
  explicit BlobVerifier(BlobfsMetrics* metrics);

  // TODO(44742): Make this type movable (requires digest::* member classes to be move-friendly).
  BlobVerifier(const BlobVerifier&) = delete;
  BlobVerifier& operator=(const BlobVerifier&) = delete;

  // Verifies the tail between |data_size| and |buffer_size| is zeroed.
  [[nodiscard]] zx_status_t VerifyTailZeroed(const void* data, size_t data_size,
                                             size_t buffer_size);

  const BlobCorruptionNotifier* corruption_notifier_;
  digest::Digest digest_;
  digest::MerkleTreeVerifier tree_verifier_;
  BlobfsMetrics* metrics_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_

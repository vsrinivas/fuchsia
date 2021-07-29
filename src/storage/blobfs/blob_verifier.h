// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_
#define SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_

#include <lib/zx/status.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <fbl/macros.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/storage/blobfs/blob_corruption_notifier.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/metrics.h"

namespace blobfs {

// BlobVerifier verifies the contents of a blob against a merkle tree. Thread-safe.
class BlobVerifier {
 public:
  // Creates an instance of BlobVerifier for blobs named |digest|, using the provided merkle tree
  // which is at most |merkle_size| bytes. The passed-in BlobfsMetrics will be updated when this
  // class runs.
  //
  // The passed-in mapped merkle_data_blocks VMO contains the blocks on disk. See the
  // merkle_data_blocks_ member variable below for more.
  //
  // Returns an error if the merkle tree's root does not match |digest|, or if the required tree
  // size for |data_size| bytes is bigger than |merkle_size|.
  [[nodiscard]] static zx::status<std::unique_ptr<BlobVerifier>> Create(
      digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics,
      fzl::OwnedVmoMapper merkle_data_blocks, const BlobLayout& layout,
      const BlobCorruptionNotifier* notifier);

  // Creates an instance of BlobVerifier for blobs named |digest|, which are small enough to not
  // have a stored merkle tree (i.e. MerkleTreeBytes(data_size) == 0). The passed-in BlobfsMetrics
  // will be updated when this class runs.
  [[nodiscard]] static zx::status<std::unique_ptr<BlobVerifier>> CreateWithoutTree(
      digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics, size_t data_size,
      const BlobCorruptionNotifier* notifier);

  // Verifies the entire contents of a blob. |buffer_size| is the total size of the buffer and the
  // buffer must be zeroed from |data_size| to |buffer_size|.
  // TODO(fxbug.dev/45457): Make const if MerkleTreeVerifier::Verify becomes const
  [[nodiscard]] zx_status_t Verify(const void* data, size_t data_size, size_t buffer_size);

  // Verifies a range of the contents of a blob from [data_offset, data_offset + length).
  // IMPORTANT: |data| is expected to be a pointer to the blob's contents at |data_offset|, not the
  // absolute start of the blob's data. (This facilitates partial verification when the blob is only
  // partially mapped in.) |buffer_size| is the total size of the buffer (relative to |data|) and
  // the buffer must be zerored from |data_size| to |buffer_size|.
  // TODO(fxbug.dev/45457): Make const if MerkleTreeVerifier::Verify becomes const
  [[nodiscard]] zx_status_t VerifyPartial(const void* data, size_t length, size_t data_offset,
                                          size_t buffer_size);

  // Modifies |data_off| and |buf_len| to be aligned to the minimum number of merkle tree nodes that
  // covered their original range.
  [[nodiscard]] zx_status_t Align(size_t* data_off, size_t* buf_len) const {
    return tree_verifier_.Align(data_off, buf_len);
  }

  const Digest& digest() { return digest_; }

 private:
  friend class BlobLoaderTest;

  // Use |Create| or |CreateWithoutTree| to construct.
  explicit BlobVerifier(digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics);

  BlobVerifier(const BlobVerifier&) = delete;
  BlobVerifier& operator=(const BlobVerifier&) = delete;

  // When created with a Merkle tree, this owns the data that holds the tree. This backs the pointer
  // provided the the tree_verifier_. This is block-aligned as on disk, consult the BlobLayout
  // object for its offset and length inside of this buffer.
  fzl::OwnedVmoMapper merkle_data_blocks_;

  const BlobCorruptionNotifier* corruption_notifier_;
  const digest::Digest digest_;

  // The verification lock is to be used whenever calling Verify() on the |tree_verifier_| since
  // the call mutates internal state variables which makes the call not thread-safe. The member is
  // not guarded by the mutex because there are perfectly safe const calls within it that do not
  // require locking. Failing to take this lock when running Verify() will make this class no longer
  // thread-safe.
  std::mutex verification_lock_;
  digest::MerkleTreeVerifier tree_verifier_;
  std::shared_ptr<BlobfsMetrics> metrics_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_VERIFIER_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_LOADER_H_
#define SRC_STORAGE_BLOBFS_BLOB_LOADER_H_

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <memory>

#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <fbl/function.h>
#include <fbl/macros.h>
#include <storage/buffer/owned_vmoid.h>

#include "compression/seekable-decompressor.h"
#include "compression/zstd-seekable-blob-collection.h"
#include "iterator/block-iterator-provider.h"
#include "metrics.h"
#include "pager/page-watcher.h"
#include "transaction-manager.h"

namespace blobfs {

// BlobLoader is responsible for loading blobs from disk, decoding them and verifying their
// contents as needed.
class BlobLoader {
 public:
  BlobLoader() = default;
  BlobLoader(BlobLoader&& o) = default;
  BlobLoader& operator=(BlobLoader&& o) = default;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BlobLoader);

  // Creates a BlobLoader.
  static zx::status<BlobLoader> Create(TransactionManager* txn_manager,
                                       BlockIteratorProvider* block_iter_provider,
                                       NodeFinder* node_finder, pager::UserPager* pager,
                                       BlobfsMetrics* metrics,
                                       ZSTDSeekableBlobCollection* zstd_seekable_blob_collection);

  // Resets the BlobLoader, freeing any owned resources. |LoadBlob*| must not be invoked after
  // |Reset()| is called.
  void Reset();

  // Loads the merkle tree and data for the blob with index |node_index|.
  //
  // |data_out| will be a VMO containing all of the data of the blob, padded up to a block size.
  // |merkle_out| will be a VMO containing the merkle tree of the blob. For small blobs, there
  // may be no merkle tree (i.e. the entire 'tree' is just a single hash stored inline in the
  // inode), in which case no VMO is returned.
  //
  // This method verifies the following correctness properties:
  //  - The stored merkle tree is well-formed.
  //  - The blob's merkle root in |inode| matches the root of the merkle tree stored on-disk.
  //  - The blob's contents match the merkle tree.
  zx_status_t LoadBlob(uint32_t node_index, const BlobCorruptionNotifier* corruption_notifier,
                       fzl::OwnedVmoMapper* data_out, fzl::OwnedVmoMapper* merkle_out);
  // Loads the merkle tree for the blob referenced |inode|, and prepare a pager-backed VMO for
  // data.
  //
  // |page_watcher_out| will be a PageWatcher that is backing |data_out|.
  // |data_out| will be a pager-backed VMO with no resident pages, padded up to a block size.
  // |merkle_out| will be a VMO containing the merkle tree of the blob. For small blobs, there
  // may be no merkle tree, in which case no VMO is returned.
  //
  // This method verifies the following correctness properties:
  //  - The stored merkle tree is well-formed.
  //  - The blob's merkle root in |inode| matches the root of the merkle tree stored on-disk.
  //
  // This method does *NOT* immediately verify the integrity of the blob's data, this will be
  // lazily verified by the pager as chunks of the blob are loaded.
  zx_status_t LoadBlobPaged(uint32_t node_index, const BlobCorruptionNotifier* corruption_notifier,
                            std::unique_ptr<pager::PageWatcher>* page_watcher_out,
                            fzl::OwnedVmoMapper* data_out, fzl::OwnedVmoMapper* merkle_out);

 private:
  BlobLoader(TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
             NodeFinder* node_finder, pager::UserPager* pager, BlobfsMetrics* metrics,
             ZSTDSeekableBlobCollection* zstd_seekable_blob_collection,
             fzl::OwnedVmoMapper scratch_vmo, storage::OwnedVmoid scratch_vmoid);

  // Loads the merkle tree from disk and initializes a VMO mapping and BlobVerifier with the
  // contents. (Small blobs may have no stored tree, in which case |vmo_out| is not mapped but
  // |verifier_out| is still initialized.)
  zx_status_t InitMerkleVerifier(uint32_t node_index, const Inode& inode,
                                 const BlobCorruptionNotifier* corruption_notifier,
                                 fzl::OwnedVmoMapper* vmo_out,
                                 std::unique_ptr<BlobVerifier>* verifier_out);
  // Prepares |decompressor_out| to decompress the blob contents of |inode|.
  // If |inode| is not compressed, this is a NOP.
  // Depending on the format, some data may be read (e.g. for the CHUNKED format, the header
  // containing the seek table is read and used to initialize the decomprssor).
  zx_status_t InitForDecompression(uint32_t node_index, const Inode& inode,
                                   const BlobVerifier& verifier,
                                   std::unique_ptr<SeekableDecompressor>* decompressor_out,
                                   ZSTDSeekableBlobCollection** zstd_seekable_blob_collection_out);
  zx_status_t LoadMerkle(uint32_t node_index, const Inode& inode,
                         const fzl::OwnedVmoMapper& vmo) const;
  zx_status_t LoadData(uint32_t node_index, const Inode& inode,
                       const fzl::OwnedVmoMapper& vmo) const;
  zx_status_t LoadAndDecompressData(uint32_t node_index, const Inode& inode,
                                    const fzl::OwnedVmoMapper& vmo) const;

  // The out duration will only be valid when Cobalt metrics are enabled. Otherwise it will be 0.
  zx_status_t LoadDataInternal(uint32_t node_index, const Inode& inode,
                               const fzl::OwnedVmoMapper& vmo, fs::Duration* out_duration,
                               uint64_t* out_bytes_read) const;

  TransactionManager* txn_manager_ = nullptr;
  BlockIteratorProvider* block_iter_provider_ = nullptr;
  NodeFinder* node_finder_ = nullptr;
  pager::UserPager* pager_ = nullptr;
  BlobfsMetrics* metrics_ = nullptr;
  ZSTDSeekableBlobCollection* zstd_seekable_blob_collection_ = nullptr;
  fzl::OwnedVmoMapper scratch_vmo_;
  storage::OwnedVmoid scratch_vmoid_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_LOADER_H_

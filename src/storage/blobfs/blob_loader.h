// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_LOADER_H_
#define SRC_STORAGE_BLOBFS_BLOB_LOADER_H_

#include <lib/fit/function.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/macros.h>
#include <storage/buffer/owned_vmoid.h>
#include <storage/buffer/resizeable_vmo_buffer.h>

#include "src/storage/blobfs/blob_corruption_notifier.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blob_verifier.h"
#include "src/storage/blobfs/blobfs_metrics.h"
#include "src/storage/blobfs/compression/external_decompressor.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/iterator/block_iterator_provider.h"
#include "src/storage/blobfs/loader_info.h"
#include "src/storage/blobfs/node_finder.h"
#include "src/storage/blobfs/transaction_manager.h"

namespace blobfs {

// BlobLoader is responsible for loading blobs from disk, decoding them and verifying their
// contents as needed.
class BlobLoader {
 public:
  ~BlobLoader();

  BlobLoader(BlobLoader&& o) = default;
  BlobLoader& operator=(BlobLoader&& o) = delete;

  // Creates a BlobLoader.
  static zx::result<std::unique_ptr<BlobLoader>> Create(
      TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
      NodeFinder* node_finder, std::shared_ptr<BlobfsMetrics> metrics,
      DecompressorCreatorConnector* decompression_connector);

  // Loads the merkle tree for the blob referenced |inode|, and prepare a pager-backed VMO for
  // data.
  //
  // This method verifies the following correctness properties:
  //  - The stored merkle tree is well-formed.
  //  - The blob's merkle root in |inode| matches the root of the merkle tree stored on-disk.
  //
  // This method does *NOT* immediately verify the integrity of the blob's data, this will be
  // lazily verified by the pager as chunks of the blob are loaded.
  zx::result<LoaderInfo> LoadBlob(uint32_t node_index,
                                  const BlobCorruptionNotifier* corruption_notifier);

 private:
  BlobLoader(TransactionManager* txn_manager, BlockIteratorProvider* block_iter_provider,
             NodeFinder* node_finder, std::shared_ptr<BlobfsMetrics> metrics,
             storage::ResizeableVmoBuffer read_mapper, zx::vmo sandbox_vmo,
             std::unique_ptr<ExternalDecompressorClient> decompressor_client);

  // Loads the merkle tree from disk and initializes a VMO mapping and BlobVerifier with the
  // contents.
  zx::result<std::unique_ptr<BlobVerifier>> CreateBlobVerifier(
      uint32_t node_index, const Inode& inode, const BlobLayout& blob_layout,
      const BlobCorruptionNotifier* corruption_notifier);

  // Prepares |decompressor_out| to decompress the blob contents of |inode|.
  // If |inode| is not compressed, this is a NOP.
  // Depending on the format, some data may be read (e.g. for the CHUNKED format, the header
  // containing the seek table is read and used to initialize the decompressor).
  zx_status_t InitForDecompression(uint32_t node_index, const Inode& inode,
                                   const BlobLayout& blob_layout, const BlobVerifier& verifier,
                                   std::unique_ptr<SeekableDecompressor>* decompressor_out);

  // Reads |block_count| blocks starting at |block_offset| from the blob specified by |node_index|
  // and returns a span pointing to the data read (which will be contained within read_mapper_).
  // The span will remain valid until the next call to LoadBlocks or Decommit is called.
  zx::result<cpp20::span<const uint8_t>> LoadBlocks(uint32_t node_index, uint64_t block_offset,
                                                    uint64_t block_count);

  // Returns the block size used by blobfs.
  uint64_t GetBlockSize() const;

  // Decommit any temporary memory owned by the loader.
  void Decommit();

  TransactionManager* txn_manager_ = nullptr;
  BlockIteratorProvider* block_iter_provider_ = nullptr;
  NodeFinder* node_finder_ = nullptr;
  std::shared_ptr<BlobfsMetrics> metrics_;
  storage::ResizeableVmoBuffer read_mapper_;
  zx::vmo sandbox_vmo_;
  std::unique_ptr<ExternalDecompressorClient> decompressor_client_ = nullptr;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_LOADER_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_COLLECTION_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_COLLECTION_H_

#include <lib/fzl/owned-vmo-mapper.h>
#include <stdint.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/node-finder.h>
#include <fbl/macros.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <storage/buffer/owned_vmoid.h>
#include <storage/buffer/vmoid_registry.h>

#include "allocator/allocator.h"
#include "compression/zstd-seekable.h"

namespace blobfs {

// The number of bytes for the singleton transfer buffer that reads from storage in the compressed
// case. The choice to use a singleton buffer is somewhat arbitrary, but it simplifies code that
// would otherwise have to manage a pool of buffers or create and destroy them for every blob.
//
// This is analagous to |kTransferBufferSize| for compressed blobs. The buffer must be large enough
// to comfortably service individual reads in compressed space from any supported decompression
// strategy. Unlike the uncompressed case, pages are not passed off to client-owned VMOs in this
// case, so pages will not be decommitted by construction. Hence, this value should be sufficiently
// large but no larger.
constexpr uint64_t kCompressedTransferBufferBytes = fbl::round_up(
    std::max<uint64_t>(kZSTDSeekableHeaderSize, kZSTDSeekableMaxFrameSize), kBlobfsBlockSize);

// The number of blocks for the singleton transfer buffer that reads from storage in the compressed
// case. Due to the types used in contexts that need this value, it is necessary to be assured that
// it fits inside a |uint32_t|.
static_assert(kCompressedTransferBufferBytes / kBlobfsBlockSize <=
              std::numeric_limits<uint32_t>::max());
constexpr uint32_t kCompressedTransferBufferBlocks =
    static_cast<uint32_t>(kCompressedTransferBufferBytes / kBlobfsBlockSize);

// ZSTDSeekableBlobCollection is a container for accessing compressed blobs. This container stores
// data shared between compressed blobs such as a single storage/VMO transfer buffer.
class ZSTDSeekableBlobCollection {
 public:
  static zx_status_t Create(storage::VmoidRegistry* vmoid_registry, SpaceManager* space_manager,
                            fs::LegacyTransactionHandler* txn_handler, NodeFinder* node_finder,
                            std::unique_ptr<ZSTDSeekableBlobCollection>* out);

  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDSeekableBlobCollection);

  // Load exactly |num_bytes| bytes starting at _uncompressed_ file contents byte offset
  // |data_byte_offset| from blob identified by inode index |node_index| into |buf|. The value of
  // data in |buf| is expected to be valid if and only if the return value is |ZX_OK|.
  zx_status_t Read(uint32_t node_index, uint8_t* buf, uint64_t data_byte_offset,
                   uint64_t num_bytes);

 private:
  ZSTDSeekableBlobCollection(storage::VmoidRegistry* vmoid_registry, SpaceManager* space_manager,
                             fs::LegacyTransactionHandler* txn_handler, NodeFinder* node_finder);

  // Parameters passed through to |ZSTDCompressedBlockCollection| construction.
  SpaceManager* space_manager_;
  fs::LegacyTransactionHandler* txn_handler_;
  NodeFinder* node_finder_;

  // Storage transfer VMO's mapping in memory and ID from binding it to a block device.
  // It is safe to keep this VMO mapped and pass it to inidividual blobs for each read because
  // all components involved in compressed blob reads:
  //
  // 1. Run in the same thread,
  //    and
  // 2. Synchronously wait for their data to arrive in |transfer_vmo_|, then:
  //      a) Decompress and discard the data before requesting more,
  //         or
  //      b) Copy the data before requesting more.
  fzl::OwnedVmoMapper mapped_vmo_;
  storage::OwnedVmoid vmoid_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_COLLECTION_H_

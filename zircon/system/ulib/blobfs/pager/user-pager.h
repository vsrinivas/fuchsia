// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/pager.h>

#include <memory>

#include "../blob-verifier.h"
#include "../compression/seekable-decompressor.h"

namespace blobfs {

// Info required by the user pager to read in and verify pages.
// Initialized by the PageWatcher and passed on to the UserPager.
struct UserPagerInfo {
  // Unique identifier used by UserPager to identify the data source on the underlying block
  // device.
  uint32_t identifier = 0;
  // Block offset (in bytes) the data starts at. Used to inform the UserPager of the offset it
  // should start issuing reads from.
  uint64_t data_start_bytes = 0;
  // Total length of the data. The |verifier| must be set up to verify this length.
  uint64_t data_length_bytes = 0;
  // Used to verify the pages as they are read in.
  // TODO(44742): Make BlobVerifier movable, unwrap from unique_ptr.
  std::unique_ptr<BlobVerifier> verifier;
  // An optional decompressor which should be applied to the raw bytes received from the disk.
  // If unset, the data is assumed to be uncompressed and is not modified.
  std::unique_ptr<SeekableDecompressor> decompressor;
};

// The size of a transfer buffer for reading from storage.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kTransferBufferSize = 256 * (1 << 20);

// The size of a transfer buffer for reading from storage.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kDecompressionBufferSize = 256 * (1 << 20);

// Abstract class that encapsulates a user pager, its associated thread and transfer buffer. The
// child class will need to define the interface required to populate the transfer buffer with
// blocks read from storage.
class UserPager {
 public:
  UserPager() = default;
  virtual ~UserPager() = default;

  // Returns the pager handle.
  const zx::pager& Pager() const { return pager_; }

  // Returns the pager dispatcher.
  async_dispatcher_t* Dispatcher() const { return pager_loop_.dispatcher(); }

  // Invoked by the |PageWatcher| on a read request. Reads in the requested byte range
  // [|offset|, |offset| + |length|) for the inode associated with |info->identifier| into the
  // |transfer_buffer_|, and then moves those pages to the destination |vmo|. If |verifier_info| is
  // not null, uses it to verify the pages prior to transferring them to the destination vmo.
  [[nodiscard]] zx_status_t TransferPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                               UserPagerInfo* info);

 protected:
  // Sets up the transfer buffer, creates the pager and starts the pager thread.
  [[nodiscard]] zx_status_t InitPager();

  // Protected for unit test access.
  zx::pager pager_;

 private:
  struct ReadRange {
    uint64_t offset;
    uint64_t length;
  };
  // Returns a range which covers [offset, offset+length), adjusted for alignment.
  //
  // The returned range will have the following guarantees:
  //  - The range will contain [offset, offset+length).
  //  - The returned offset will be block-aligned.
  //  - The end of the returned range is *either* block-aligned or is the end of the file.
  //  - The range will be adjusted for verification (see |BlobVerifier::Align|).
  //
  // The range needs to be extended before actually populating the transfer buffer with pages, as
  // absent pages will cause page faults during verification on the userpager thread, causing it to
  // block against itself indefinitely.
  //
  // For example:
  //                  |...input_range...|
  // |..data_block..|..data_block..|..data_block..|
  //                |........output_range.........|
  ReadRange GetBlockAlignedReadRange(UserPagerInfo* info, uint64_t offset, uint64_t length);
  // Returns a range at least as big as GetBlockAlignedReadRange(), extended by an implementation
  // defined read-ahead algorithm.
  //
  // The same alignment guarantees for GetBlockAlignedReadRange() apply.
  ReadRange GetBlockAlignedExtendedRange(UserPagerInfo* info, uint64_t offset, uint64_t length);

  zx_status_t TransferCompressedPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                           UserPagerInfo* info);
  zx_status_t TransferUncompressedPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                             UserPagerInfo* info);
  // Attaches the transfer buffer to the underlying block device, so that blocks can be read into it
  // from storage.
  [[nodiscard]] virtual zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) = 0;

  // Reads data for the inode corresponding to |info->identifier| into the transfer buffer for the
  // byte range specified by [|offset|, |offset| + |length|).
  [[nodiscard]] virtual zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length,
                                                        UserPagerInfo* info) = 0;

  // Verifies the data read in to |transfer_vmo| (i.e. the transfer buffer) via
  // |PopulateTransferVmo|. Data in the range [|offset|, |offset| + |length|) is verified using the
  // |info| provided. |buffer_length| might be larger than |length| e.g. for the tail where
  // |length| might not be aligned, in which case the range between |length| and |buffer_length|
  // should be verified to be zero.
  [[nodiscard]] virtual zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length,
                                                      uint64_t buffer_length,
                                                      const zx::vmo& transfer_vmo,
                                                      UserPagerInfo* info) = 0;

  // Scratch buffer for pager transfers.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this needs to be unmapped before
  // calling |zx_pager_supply_pages|. Map this only when an explicit address is required, e.g. for
  // verification, and unmap it immediately after.
  zx::vmo transfer_buffer_;

  // Scratch buffer for decompression.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this needs to be unmapped before
  // calling |zx_pager_supply_pages|.
  zx::vmo decompression_buffer_;

  // Async loop for pager requests.
  async::Loop pager_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_

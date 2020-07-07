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
#include "../compression/zstd-seekable-blob-collection.h"

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
  // An optional decompressor used by the chunked compression strategy. The decompressor is invoked
  // on the raw bytes received from the disk. If unset, blob data is assumed to be managed via some
  // other compression strategy (including the "uncompressed" strategy).
  std::unique_ptr<SeekableDecompressor> decompressor;
  // An optional blobs management object used by the ZSTD Seekable compression strategy. If unset,
  // blob data is assumed to be managed via some other compression strategy (including the
  // "uncompressed" strategy). Note that this object is global to the |Blobfs| instance, and is
  // copied here to maintain short-term consistency between |UserPager| strategy implementations.
  //
  // TODO(51072): Decompression strategies should have common abstractions to, among other things,
  // avoid the need for both |decompressor| and |zstd_seekable_blob_collection|. This change is
  // somewhat complicated by the fact that ZSTD Seekable decompression manages its own
  // compressed-space buffer rather than reusing |transfer_buffer_| as chunked decompression does.
  ZSTDSeekableBlobCollection* zstd_seekable_blob_collection = nullptr;
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

// Wrapper enum for error codes supported by the zx_pager_op_range(ZX_PAGER_OP_FAIL) syscall, used
// to communicate userpager errors to the kernel, so that the error can be propagated to the
// originator of the page request (if required), and the waiting thread can be unblocked. We use
// this wrapper enum instead of a raw zx_status_t as not all error codes are supported.
enum class PagerErrorStatus : zx_status_t {
  kErrIO = ZX_ERR_IO,
  kErrDataIntegrity = ZX_ERR_IO_DATA_INTEGRITY,
  kErrBadState = ZX_ERR_BAD_STATE,
  // This value is not supported by zx_pager_op_range(). Instead, it is used to determine if the
  // zx_pager_op_range() call is required in the first place - PagerErrorStatus::kOK indicates no
  // error, so we don't make the call.
  kOK = ZX_OK,
};

constexpr PagerErrorStatus ToPagerErrorStatus(zx_status_t status) {
  switch (status) {
    case ZX_OK:
      return PagerErrorStatus::kOK;
    // ZX_ERR_IO_DATA_INTEGRITY is the only error code in the I/O class of errors that we
    // distinguish. For everything else return ZX_ERR_IO.
    case ZX_ERR_IO_DATA_INTEGRITY:
      return PagerErrorStatus::kErrDataIntegrity;
    case ZX_ERR_IO:
    case ZX_ERR_IO_DATA_LOSS:
    case ZX_ERR_IO_INVALID:
    case ZX_ERR_IO_MISSED_DEADLINE:
    case ZX_ERR_IO_NOT_PRESENT:
    case ZX_ERR_IO_OVERRUN:
    case ZX_ERR_IO_REFUSED:
      return PagerErrorStatus::kErrIO;
    // Return ZX_ERR_BAD_STATE by default.
    default:
      return PagerErrorStatus::kErrBadState;
  }
}

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
  //
  // If an error is encountered, the error code is returned as a |PagerErrorStatus|. This error code
  // is communicated to the kernel with the zx_pager_op_range(ZX_PAGER_OP_FAIL) syscall.
  // |PagerErrorStatus| wraps the supported error codes for ZX_PAGER_OP_FAIL.
  // 1. ZX_ERR_IO - Failure while reading the required data from the block device.
  // 2. ZX_ERR_IO_DATA_INTEGRITY - Failure while verifying the contents read in.
  // 3. ZX_ERR_BAD_STATE - Any other type of failure which prevents us from successfully completing
  // the page request, e.g. failure while decompressing, or while transferring pages from the
  // transfer buffer etc.
  [[nodiscard]] PagerErrorStatus TransferPagesToVmo(uint64_t offset, uint64_t length,
                                                    const zx::vmo& vmo, const UserPagerInfo& info);

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
  ReadRange GetBlockAlignedReadRange(const UserPagerInfo& info, uint64_t offset, uint64_t length);
  // Returns a range at least as big as GetBlockAlignedReadRange(), extended by an implementation
  // defined read-ahead algorithm.
  //
  // The same alignment guarantees for GetBlockAlignedReadRange() apply.
  ReadRange GetBlockAlignedExtendedRange(const UserPagerInfo& info, uint64_t offset,
                                         uint64_t length);

  PagerErrorStatus TransferChunkedPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                             const UserPagerInfo& info);
  PagerErrorStatus TransferZSTDSeekablePagesToVmo(uint64_t offset, uint64_t length,
                                                  const zx::vmo& vmo, const UserPagerInfo& info);
  PagerErrorStatus TransferUncompressedPagesToVmo(uint64_t offset, uint64_t length,
                                                  const zx::vmo& vmo, const UserPagerInfo& info);
  // Attaches the transfer buffer to the underlying block device, so that blocks can be read into it
  // from storage.
  [[nodiscard]] virtual zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) = 0;

  // Reads data for the inode corresponding to |info.identifier| into the transfer buffer for the
  // byte range specified by [|offset|, |offset| + |length|).
  [[nodiscard]] virtual zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length,
                                                        const UserPagerInfo& info) = 0;

  // Verifies the data read in to |transfer_vmo| (i.e. the transfer buffer) via
  // |PopulateTransferVmo|. Data in the range [|offset|, |offset| + |length|) is verified using the
  // |info| provided. |buffer_length| might be larger than |length| e.g. for the tail where
  // |length| might not be aligned, in which case the range between |length| and |buffer_length|
  // should be verified to be zero.
  [[nodiscard]] virtual zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length,
                                                      uint64_t buffer_length,
                                                      const zx::vmo& transfer_vmo,
                                                      const UserPagerInfo& info) = 0;

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

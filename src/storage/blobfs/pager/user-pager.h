// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_H_
#define SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/pager.h>

#include <memory>

#include "../metrics.h"
#include "../transaction-manager.h"
#include "src/storage/lib/watchdog/include/lib/watchdog/watchdog.h"
#include "transfer-buffer.h"
#include "user-pager-info.h"
namespace blobfs {
namespace pager {

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

// Make sure blocks are page-aligned.
static_assert(kBlobfsBlockSize % PAGE_SIZE == 0);
// Make sure the pager transfer buffer is block-aligned.
static_assert(kTransferBufferSize % kBlobfsBlockSize == 0);

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

// Encapsulates a user pager, its associated thread and transfer buffer.
class UserPager {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UserPager);

  // Creates an instance of UserPager.
  // A new thread is created and started to process page fault requests.
  // |buffer| is used to retrieve and buffer data from the underlying storage.
  [[nodiscard]] static zx::status<std::unique_ptr<UserPager>> Create(
      std::unique_ptr<TransferBuffer> buffer, BlobfsMetrics* metrics);

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
  // Protected for unit test access.
  zx::pager pager_;

 private:
  explicit UserPager(BlobfsMetrics* metrics);

  // Helper function to apply a scheduling deadline profile to the pager |thread|. Called from
  // |UserPager::Create| after starting the pager thread.
  static void SetDeadlineProfile(thrd_t thread);

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

  // Scratch buffer for pager transfers.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, the VMO owned by this buffer
  // needs to be unmapped before calling |zx_pager_supply_pages|. Map |transfer_buffer_.vmo()| only
  // when an explicit address is required, e.g. for verification, and unmap it immediately after.
  std::unique_ptr<TransferBuffer> transfer_buffer_;

  // Scratch buffer for decompression.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this needs to be unmapped before
  // calling |zx_pager_supply_pages|.
  zx::vmo decompression_buffer_;

  // Watchdog which triggers if any page faults exceed a threshold deadline.  This *must* come
  // before the loop below so that the loop, which might have references to the watchdog, is
  // destryoed first.
  std::unique_ptr<fs_watchdog::WatchdogInterface> watchdog_;

  // Async loop for pager requests.
  async::Loop pager_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Records all metrics for this instance of blobfs.
  BlobfsMetrics* metrics_ = nullptr;
};

}  // namespace pager
}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_PAGER_USER_PAGER_H_

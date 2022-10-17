// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_PAGE_LOADER_H_
#define SRC_STORAGE_BLOBFS_PAGE_LOADER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/pager.h>

#include <functional>
#include <memory>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/storage/blobfs/blobfs_metrics.h"
#include "src/storage/blobfs/compression/external_decompressor.h"
#include "src/storage/blobfs/loader_info.h"
#include "src/storage/blobfs/transaction_manager.h"
#include "src/storage/blobfs/transfer_buffer.h"
#include "src/storage/lib/watchdog/include/lib/watchdog/watchdog.h"

namespace blobfs {

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
    case ZX_ERR_PEER_CLOSED:
      return PagerErrorStatus::kErrIO;
    // Return ZX_ERR_BAD_STATE by default.
    default:
      return PagerErrorStatus::kErrBadState;
  }
}

// Applies the scheduling deadline profile to the given pager thread.
void SetDeadlineProfile(const std::vector<zx::unowned_thread>& threads);

// Encapsulates a user pager, its associated thread and transfer buffer.
class PageLoader {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(PageLoader);

  // Resources needed for each worker thread.
  struct WorkerResources {
    WorkerResources(std::unique_ptr<TransferBuffer> uncompressed,
                    std::unique_ptr<TransferBuffer> compressed)
        : uncompressed_buffer(std::move(uncompressed)), compressed_buffer(std::move(compressed)) {}

    std::unique_ptr<TransferBuffer> uncompressed_buffer;
    std::unique_ptr<TransferBuffer> compressed_buffer;
  };

  // Abstracts out how pages are supplied to the system.
  using PageSupplier = std::function<zx::result<>(uint64_t offset, uint64_t length,
                                                  const zx::vmo& aux_vmo, uint64_t aux_offset)>;

  // Creates an instance of PageLoader.
  // A new thread is created and started to process page fault requests.
  // |uncompressed_buffer| is used to retrieve and buffer uncompressed data from the underlying
  // storage. |resources| is a set of resources needed for each individual |Worker| so only as many
  // pager threads are supported as there are sets of resources. |decompression_buffer_size| is the
  // size of the scratch buffer to use for decompression.
  [[nodiscard]] static zx::result<std::unique_ptr<PageLoader>> Create(
      std::vector<std::unique_ptr<WorkerResources>> resources, size_t decompression_buffer_size,
      BlobfsMetrics* metrics, DecompressorCreatorConnector* decompression_connector);

  // Invoked on a read request. Reads in the requested byte range [|offset|, |offset| + |length|)
  // for the inode associated with |info->identifier| into the |transfer_buffer_|, and then moves
  // those pages to the destination |vmo|. If |verifier_info| is not null, uses it to verify the
  // pages prior to transferring them to the destination vmo.
  //
  // How the pages are supplied to the caller is defined by the PageSupplier callback parameter.
  //
  // If an error is encountered, the error code is returned as a |PagerErrorStatus|. This error code
  // is communicated to the kernel with the zx_pager_op_range(ZX_PAGER_OP_FAIL) syscall.
  // |PagerErrorStatus| wraps the supported error codes for ZX_PAGER_OP_FAIL.
  // 1. ZX_ERR_IO - Failure while reading the required data from the block device.
  // 2. ZX_ERR_IO_DATA_INTEGRITY - Failure while verifying the contents read in.
  // 3. ZX_ERR_BAD_STATE - Any other type of failure which prevents us from successfully completing
  // the page request, e.g. failure while decompressing, or while transferring pages from the
  // transfer buffer etc.
  [[nodiscard]] PagerErrorStatus TransferPages(const PageSupplier& page_supplier, uint64_t offset,
                                               uint64_t length, const LoaderInfo& info);

 private:
  class Worker {
   public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Worker);
    // Creates a Worker resource. This resource is not thread-safe and should be associated with a
    // single thread, or protected via mutex, while serving page faults.
    // |uncompressed_buffer| is used to retrieve and buffer uncompressed data from the underlying
    // storage. |compressed_buffer| is used to retrieve and buffer compressed data from the
    // underlying storage. |decompression_buffer_size| is the size of the scratch buffer to use for
    // decompression.
    [[nodiscard]] static zx::result<std::unique_ptr<Worker>> Create(
        std::unique_ptr<WorkerResources> resources, size_t decompression_buffer_size,
        BlobfsMetrics* metrics, DecompressorCreatorConnector* decompression_connector);

    // See |PageLoader::TransferPages()| which simply selects which Worker to delegate the
    // actual work to.
    [[nodiscard]] PagerErrorStatus TransferPages(const PageLoader::PageSupplier& page_supplier,
                                                 uint64_t offset, uint64_t length,
                                                 const LoaderInfo& info);

   private:
    Worker(size_t decompression_buffer_size, BlobfsMetrics* metrics);

    PagerErrorStatus TransferChunkedPages(const PageLoader::PageSupplier& page_supplier,
                                          uint64_t offset, uint64_t length, const LoaderInfo& info);
    PagerErrorStatus TransferUncompressedPages(const PageLoader::PageSupplier& page_supplier,
                                               uint64_t offset, uint64_t length,
                                               const LoaderInfo& info);

    // Scratch buffer for pager transfers of uncompressed data.
    // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, the VMO owned by this buffer
    // needs to be unmapped before calling |zx_pager_supply_pages|. Map
    // |uncompressed_transfer_buffer_.vmo()| only when an explicit address is required, e.g. for
    // verification, and unmap it immediately after.
    std::unique_ptr<TransferBuffer> uncompressed_transfer_buffer_;

    // Scratch buffer for pager transfers of compressed data.
    // Unlike the above transfer buffer, this never needs to be unmapped since we will be calling
    // |zx_pager_supply_pages| on the |decompression_buffer_|.
    std::unique_ptr<TransferBuffer> compressed_transfer_buffer_;

    // A persistent mapping for |compressed_transfer_buffer_|.
    fzl::VmoMapper compressed_mapper_;

    // This is the buffer that can be written to by the other end of the
    // |decompressor_client_| connection. The contents are not to be trusted and
    // may be changed at any time, so they need to be copied out prior to
    // verification.
    zx::vmo sandbox_buffer_;

    // Scratch buffer for decompression.
    // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this needs to be unmapped
    // before calling |zx_pager_supply_pages|.
    zx::vmo decompression_buffer_;

    // Size of |decompression_buffer_|, stashed at vmo creation time to avoid a syscall each time
    // the size needs to be queried.
    const size_t decompression_buffer_size_;

    // Maintains a connection to the external decompressor.
    std::unique_ptr<ExternalDecompressorClient> decompressor_client_;

    // Records all metrics for this instance of blobfs.
    BlobfsMetrics* metrics_ = nullptr;
  };

  explicit PageLoader(std::vector<std::unique_ptr<Worker>> workers);

  // Watchdog which triggers if any page faults exceed a threshold deadline.  This *must* come
  // before the loop below so that the loop, whose threads might have references to the watchdog, is
  // destroyed first.
  std::unique_ptr<fs_watchdog::WatchdogInterface> watchdog_;

  // Called for instantiating a thread local which statically assigns a worker to each thread.
  uint32_t AllocateWorker();

  // Set of resources required by a thread to serve the page. This *must* come before the loop below
  // since the threads for that loop may have references to the worker. Destruction of that loop
  // will ensure that all of the threads have stopped.
  std::vector<std::unique_ptr<Worker>> workers_;

  // Protects the worker_id_allocator_ from multiple pager thread races.
  std::mutex worker_allocation_lock_;
  // Incremented on the first call within each pager thread to allocate a worker.
  uint32_t worker_id_allocator_ __TA_GUARDED(worker_allocation_lock_) = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_PAGE_LOADER_H_

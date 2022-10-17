// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/page_loader.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/thread.h>
#include <limits.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <algorithm>
#include <memory>

#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/blobfs/blobfs_metrics.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/lib/watchdog/include/lib/watchdog/operations.h"

namespace blobfs {

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
ReadRange GetBlockAlignedReadRange(const LoaderInfo& info, uint64_t offset, uint64_t length) {
  uint64_t uncompressed_byte_length = info.layout->FileSize();
  ZX_DEBUG_ASSERT(offset < uncompressed_byte_length);
  // Clamp the range to the size of the blob.
  length = std::min(length, uncompressed_byte_length - offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  zx_status_t status = info.verifier->Align(&offset, &length);
  // This only happens if the info.verifier thinks that [offset,length) is out of range, which
  // will only happen if |verifier| was initialized with a different length than the rest of |info|
  // (which is a programming error).
  ZX_DEBUG_ASSERT(status == ZX_OK);

  ZX_DEBUG_ASSERT(offset % kBlobfsBlockSize == 0);
  ZX_DEBUG_ASSERT(length % kBlobfsBlockSize == 0 || offset + length == uncompressed_byte_length);

  return {.offset = offset, .length = length};
}

// Returns a range at least as big as GetBlockAlignedReadRange(), extended by an implementation
// defined read-ahead algorithm.
//
// The same alignment guarantees for GetBlockAlignedReadRange() apply.
ReadRange GetBlockAlignedExtendedRange(const LoaderInfo& info, uint64_t offset, uint64_t length) {
  // TODO(rashaeqbal): Consider making the cluster size dynamic once we have prefetch read
  // efficiency metrics from the kernel - i.e. what percentage of prefetched pages are actually
  // used. Note that dynamic prefetch sizing might not play well with compression, since we
  // always need to read in entire compressed frames.
  //
  // TODO(rashaeqbal): Consider extending the range backwards as well. Will need some way to track
  // populated ranges.
  //
  // Read in at least 32KB at a time. This gives us the best performance numbers w.r.t. memory
  // savings and observed latencies. Detailed results from experiments to tune this can be found in
  // fxbug.dev/48519.
  constexpr uint64_t kReadAheadClusterSize{UINT64_C(32) * (1 << 10)};

  size_t read_ahead_offset = offset;
  size_t read_ahead_length = std::max(kReadAheadClusterSize, length);
  read_ahead_length = std::min(read_ahead_length, info.layout->FileSize() - read_ahead_offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  return GetBlockAlignedReadRange(info, read_ahead_offset, read_ahead_length);
}

void SetDeadlineProfile(const std::vector<zx::unowned_thread>& threads) {
  zx::channel channel0, channel1;
  zx_status_t status = zx::channel::create(0u, &channel0, &channel1);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Could not create channel pair: " << zx_status_get_string(status);
    return;
  }

  // Connect to the scheduler profile provider service.
  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
      channel0.release());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Could not connect to scheduler profile provider: "
                     << zx_status_get_string(status);
    return;
  }

  fuchsia::scheduler::ProfileProvider_SyncProxy provider(std::move(channel1));

  zx_status_t fidl_status = ZX_OK;
  zx::profile profile;

  // Deadline profile parameters for the pager thread.
  // Details on the performance analysis to arrive at these numbers can be found in fxbug.dev/56291.
  //
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  const zx_duration_t capacity = ZX_USEC(1800);
  const zx_duration_t deadline = ZX_USEC(2800);
  const zx_duration_t period = deadline;

  status = provider.GetDeadlineProfile(
      capacity, deadline, period, "/boot/bin/blobfs:blobfs-pager-thread", &fidl_status, &profile);

  if (status != ZX_OK || fidl_status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to get deadline profile: " << zx_status_get_string(status) << ", "
                     << zx_status_get_string(fidl_status);
    return;
  }

  // Apply to each thread.
  for (const auto& thread : threads) {
    if (zx_status_t status = thread->set_profile(profile, 0); status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to set deadline profile: " << zx_status_get_string(status);
    }
  }
}

PageLoader::Worker::Worker(size_t decompression_buffer_size, BlobfsMetrics* metrics)
    : decompression_buffer_size_(decompression_buffer_size), metrics_(metrics) {}

zx::result<std::unique_ptr<PageLoader::Worker>> PageLoader::Worker::Create(
    std::unique_ptr<WorkerResources> resources, size_t decompression_buffer_size,
    BlobfsMetrics* metrics, DecompressorCreatorConnector* decompression_connector) {
  ZX_DEBUG_ASSERT(metrics != nullptr && resources->uncompressed_buffer != nullptr &&
                  resources->uncompressed_buffer->GetVmo().is_valid() &&
                  resources->compressed_buffer != nullptr &&
                  resources->compressed_buffer->GetVmo().is_valid());

  if (resources->uncompressed_buffer->GetSize() % kBlobfsBlockSize ||
      resources->compressed_buffer->GetSize() % kBlobfsBlockSize ||
      decompression_buffer_size % kBlobfsBlockSize) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (resources->compressed_buffer->GetSize() < decompression_buffer_size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  TRACE_DURATION("blobfs", "PageLoader::Worker::Create");

  auto worker = std::unique_ptr<PageLoader::Worker>(
      new PageLoader::Worker(decompression_buffer_size, metrics));
  worker->uncompressed_transfer_buffer_ = std::move(resources->uncompressed_buffer);
  worker->compressed_transfer_buffer_ = std::move(resources->compressed_buffer);

  zx_status_t status = worker->compressed_mapper_.Map(
      worker->compressed_transfer_buffer_->GetVmo(), 0,
      worker->compressed_transfer_buffer_->GetSize(), ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map the compressed TransferBuffer: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }

  status = zx::vmo::create(worker->decompression_buffer_size_, 0, &worker->decompression_buffer_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create decompression buffer: " << zx_status_get_string(status);
    return zx::error(status);
  }

  if (decompression_connector) {
    status = zx::vmo::create(kDecompressionBufferSize, 0, &worker->sandbox_buffer_);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create sandbox buffer: %s\n" << zx_status_get_string(status);
      return zx::error(status);
    }

    auto client_or =
        ExternalDecompressorClient::Create(decompression_connector, worker->sandbox_buffer_,
                                           worker->compressed_transfer_buffer_->GetVmo());
    if (!client_or.is_ok()) {
      return zx::error(client_or.status_value());
    }
    worker->decompressor_client_ = std::move(client_or.value());
  }

  return zx::ok(std::move(worker));
}

PagerErrorStatus PageLoader::Worker::TransferPages(const PageLoader::PageSupplier& page_supplier,
                                                   uint64_t offset, uint64_t length,
                                                   const LoaderInfo& info) {
  size_t end;
  if (add_overflow(offset, length, &end)) {
    FX_LOGS(ERROR) << "pager transfer range would overflow (off=" << offset << ", len=" << length
                   << ") for blob " << info.verifier->digest();
    return PagerErrorStatus::kErrBadState;
  }

  if (info.decompressor)
    return TransferChunkedPages(page_supplier, offset, length, info);
  return TransferUncompressedPages(page_supplier, offset, length, info);
}

// The requested range is aligned in multiple steps as follows:
// 1. The range is extended to speculatively read in 32k at a time.
// 2. The extended range is further aligned for Merkle tree verification later.
// 3. This range is read in chunks equal to the size of the uncompressed_transfer_buffer_. Each
// chunk is verified as it is read in, and spliced into the destination VMO with supply_pages().
//
// The assumption here is that the transfer buffer is sized per the alignment requirements for
// Merkle tree verification. We have static asserts in place to check this assumption - the transfer
// buffer (256MB) is 8k block aligned.
PagerErrorStatus PageLoader::Worker::TransferUncompressedPages(
    const PageLoader::PageSupplier& page_supplier, uint64_t requested_offset,
    uint64_t requested_length, const LoaderInfo& info) {
  ZX_DEBUG_ASSERT(!info.decompressor);

  const auto [start_offset, total_length] =
      GetBlockAlignedExtendedRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "PageLoader::TransferUncompressedPages", "offset", start_offset,
                 "length", total_length);

  uint64_t offset = start_offset;
  uint64_t length_remaining = total_length;
  uint64_t length;

  // Read in multiples of the transfer buffer size. In practice we should only require one iteration
  // for the majority of cases, since the transfer buffer is 256MB.
  while (length_remaining > 0) {
    length = std::min(uncompressed_transfer_buffer_->GetSize(), length_remaining);

    auto decommit = fit::defer([this, length = length]() {
      // Decommit pages in the transfer buffer that might have been populated. All blobs share the
      // same transfer buffer - this prevents data leaks between different blobs.
      uncompressed_transfer_buffer_->GetVmo().op_range(
          ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize), nullptr, 0);
    });

    // Read from storage into the transfer buffer.
    auto populate_status = uncompressed_transfer_buffer_->Populate(offset, length, info);
    if (!populate_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferUncompressed: Failed to populate transfer vmo for blob "
                     << info.verifier->digest() << ": " << populate_status.status_string()
                     << ". Returning as plain IO error.";
      return PagerErrorStatus::kErrIO;
    }

    const uint64_t rounded_length = fbl::round_up<uint64_t, uint64_t>(length, PAGE_SIZE);

    // The block size is a multiple of the page size and |length| has already been block aligned. If
    // |rounded_length| is greater than |length| then |length| isn't block aligned because it's at
    // the end of the blob.  In the compact layout the Merkle tree can share the last block of the
    // data and may have been read into the transfer buffer.  The Merkle tree needs to be removed
    // before transfering the pages to the destination VMO.
    static_assert(kBlobfsBlockSize % PAGE_SIZE == 0);
    if (rounded_length > length) {
      zx_status_t status = uncompressed_transfer_buffer_->GetVmo().op_range(
          ZX_VMO_OP_ZERO, length, rounded_length - length, nullptr, 0);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "TransferUncompressed: Failed to remove Merkle tree from "
                          "transfer buffer: "
                       << zx_status_get_string(status);
        return ToPagerErrorStatus(status);
      }
    }

    // Verify the pages read in.
    {
      fzl::VmoMapper mapping;
      // We need to unmap the transfer VMO before its pages can be transferred to the destination
      // VMO, via |zx_pager_supply_pages|.
      auto unmap = fit::defer([&]() { mapping.Unmap(); });

      // Map the transfer VMO in order to pass the verifier a pointer to the data.
      zx_status_t status =
          mapping.Map(uncompressed_transfer_buffer_->GetVmo(), 0, rounded_length, ZX_VM_PERM_READ);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "TransferUncompressed: Failed to map transfer buffer: "
                       << zx_status_get_string(status);
        return ToPagerErrorStatus(status);
      }

      status = info.verifier->VerifyPartial(mapping.start(), length, offset, rounded_length);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "TransferUncompressed: Failed to verify data for blob "
                       << info.verifier->digest() << ": " << zx_status_get_string(status);
        return ToPagerErrorStatus(status);
      }
    }

    ZX_DEBUG_ASSERT(offset % PAGE_SIZE == 0);
    // Move the pages from the transfer buffer to the destination VMO.
    if (auto status =
            page_supplier(offset, rounded_length, uncompressed_transfer_buffer_->GetVmo(), 0);
        status.is_error()) {
      FX_LOGS(ERROR) << "TransferUncompressed: Failed to supply pages to paged VMO for blob "
                     << info.verifier->digest() << ": " << status.status_string();
      return ToPagerErrorStatus(status.error_value());
    }

    length_remaining -= length;
    offset += length;
  }

  fbl::String merkle_root_hash = info.verifier->digest().ToString();
  metrics_->IncrementPageIn(merkle_root_hash, start_offset, total_length);

  return PagerErrorStatus::kOK;
}

// The requested range is aligned in multiple steps as follows:
// 1. The desired uncompressed range is aligned for Merkle tree verification.
// 2. This range is extended to span complete compression frames / chunks, since that is the
// granularity we can decompress data in. The result of this alignment produces a
// CompressionMapping, which contains the mapping of the requested uncompressed range to the
// compressed range that needs to be read in from disk.
// 3. The uncompressed range is processed in chunks equal to the decompression_buffer_size_. For
// each chunk, we compute the CompressionMapping to determine the compressed range that needs to be
// read in. Each chunk is uncompressed and verified as it is read in, and spliced into the
// destination VMO with supply_pages().
//
// There are two assumptions we make here: First that the decompression buffer is sized per the
// alignment requirements for Merkle tree verification. And second that the transfer buffer is sized
// such that it can accommodate all the compressed data for the decompression buffer, i.e. the
// transfer buffer should work with the worst case compression ratio of 1. We have static asserts in
// place to check both these assumptions - the transfer buffer is the same size as the decompression
// buffer (256MB), and both these buffers are 8k block aligned.
PagerErrorStatus PageLoader::Worker::TransferChunkedPages(
    const PageLoader::PageSupplier& page_supplier, uint64_t requested_offset,
    uint64_t requested_length, const LoaderInfo& info) {
  ZX_DEBUG_ASSERT(info.decompressor);

  const auto [offset, length] = GetBlockAlignedReadRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "PageLoader::TransferChunkedPages", "offset", offset, "length", length);

  fbl::String merkle_root_hash = info.verifier->digest().ToString();

  size_t current_decompressed_offset = offset;
  size_t desired_decompressed_end = offset + length;

  // Read in multiples of the decompression buffer size. In practice we should only require one
  // iteration for the majority of cases, since the decompression buffer is 256MB.
  while (current_decompressed_offset < desired_decompressed_end) {
    size_t current_decompressed_length = desired_decompressed_end - current_decompressed_offset;
    zx::result<CompressionMapping> mapping_status = info.decompressor->MappingForDecompressedRange(
        current_decompressed_offset, current_decompressed_length, decompression_buffer_size_);

    if (!mapping_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to find range for [" << offset << ", "
                     << current_decompressed_offset + current_decompressed_length << ") for blob "
                     << info.verifier->digest() << ": " << mapping_status.status_string();
      return ToPagerErrorStatus(mapping_status.status_value());
    }
    CompressionMapping mapping = mapping_status.value();

    // The compressed frame may not fall at a block aligned address, but we read in block aligned
    // chunks. This offset will be applied to the buffer we pass to decompression.
    // TODO(jfsulliv): Caching blocks which span frames may be useful for performance.
    size_t offset_of_compressed_data = mapping.compressed_offset % kBlobfsBlockSize;

    // Read from storage into the transfer buffer.
    size_t read_offset = fbl::round_down(mapping.compressed_offset, kBlobfsBlockSize);
    size_t read_len = (mapping.compressed_length + offset_of_compressed_data);

    auto decommit_compressed = fit::defer([this, length = read_len]() {
      // Decommit pages in the transfer buffer that might have been populated. All blobs share the
      // same transfer buffer - this prevents data leaks between different blobs.
      compressed_transfer_buffer_->GetVmo().op_range(
          ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize), nullptr, 0);
    });

    auto populate_status = compressed_transfer_buffer_->Populate(read_offset, read_len, info);
    if (!populate_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to populate transfer vmo for blob "
                     << info.verifier->digest() << ": " << populate_status.status_string()
                     << ". Returning as plain IO error.";
      return PagerErrorStatus::kErrIO;
    }

    auto decommit_decompressed = fit::defer([this, length = mapping.decompressed_length]() {
      // Decommit pages in the decompression buffer that might have been populated. All blobs share
      // the same transfer buffer - this prevents data leaks between different blobs.
      decompression_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                                     nullptr, 0);
    });

    // Map the decompression VMO.
    fzl::VmoMapper decompressed_mapper;
    if (zx_status_t status =
            decompressed_mapper.Map(decompression_buffer_, 0, mapping.decompressed_length,
                                    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to map decompress buffer: "
                     << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
    }
    auto unmap_decompression = fit::defer([&]() { decompressed_mapper.Unmap(); });

    fs::Ticker ticker;
    size_t decompressed_size = mapping.decompressed_length;
    zx_status_t decompress_status;
    if (decompressor_client_) {
      ZX_DEBUG_ASSERT(sandbox_buffer_.is_valid());
      // Try to commit all of the pages ahead of time to avoid page faulting on each one while
      // decompressing.
      if (zx_status_t status = sandbox_buffer_.op_range(
              ZX_VMO_OP_COMMIT, 0, fbl::round_up(mapping.decompressed_length, kBlobfsBlockSize),
              nullptr, 0);
          status != ZX_OK) {
        FX_LOGS(INFO) << "Failed to pre-commit sanboxed buffer pages: "
                      << zx_status_get_string(status);
        ZX_DEBUG_ASSERT(false);
      }
      auto decommit_sandbox = fit::defer([this, length = mapping.decompressed_length]() {
        // Decommit pages in the sandbox buffer that might have been populated. All blobs share
        // the same sandbox buffer - this prevents data leaks between different blobs.
        sandbox_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                                 nullptr, 0);
      });
      ExternalSeekableDecompressor decompressor(decompressor_client_.get(),
                                                info.decompressor->algorithm());
      decompress_status = decompressor.DecompressRange(
          offset_of_compressed_data, mapping.compressed_length, mapping.decompressed_length);
      if (decompress_status == ZX_OK) {
        zx_status_t read_status =
            sandbox_buffer_.read(decompressed_mapper.start(), 0, mapping.decompressed_length);
        if (read_status != ZX_OK) {
          FX_LOGS(ERROR) << "TransferChunked: Failed to copy from sandbox buffer: "
                         << zx_status_get_string(read_status);
          return ToPagerErrorStatus(read_status);
        }
      }
    } else {
      // Decompress the data
      uint8_t* src = static_cast<uint8_t*>(compressed_mapper_.start()) + offset_of_compressed_data;
      decompress_status = info.decompressor->DecompressRange(
          decompressed_mapper.start(), &decompressed_size, src, mapping.compressed_length,
          mapping.decompressed_offset);
    }
    if (decompress_status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to decompress for blob " << info.verifier->digest()
                     << ": " << zx_status_get_string(decompress_status);
      return ToPagerErrorStatus(decompress_status);
    }
    metrics_->paged_read_metrics().IncrementDecompression(CompressionAlgorithm::kChunked,
                                                          decompressed_size, ticker.End(),
                                                          decompressor_client_ != nullptr);

    // Verify the decompressed pages.
    const uint64_t rounded_length =
        fbl::round_up<uint64_t, uint64_t>(mapping.decompressed_length, PAGE_SIZE);
    zx_status_t status =
        info.verifier->VerifyPartial(decompressed_mapper.start(), mapping.decompressed_length,
                                     mapping.decompressed_offset, rounded_length);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to verify data for blob "
                     << info.verifier->digest() << ": " << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
    }

    decompressed_mapper.Unmap();

    // Move the pages from the decompression buffer to the destination VMO.
    if (auto status =
            page_supplier(mapping.decompressed_offset, rounded_length, decompression_buffer_, 0);
        status.is_error()) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to supply pages to paged VMO for blob "
                     << info.verifier->digest() << ": " << status.status_string();
      return ToPagerErrorStatus(status.error_value());
    }
    metrics_->IncrementPageIn(merkle_root_hash, read_offset, read_len);

    // Advance the required decompressed offset based on how much has already been populated.
    current_decompressed_offset = mapping.decompressed_offset + mapping.decompressed_length;
  }

  return PagerErrorStatus::kOK;
}

PageLoader::PageLoader(std::vector<std::unique_ptr<Worker>> workers)
    : workers_(std::move(workers)) {}

zx::result<std::unique_ptr<PageLoader>> PageLoader::Create(
    std::vector<std::unique_ptr<WorkerResources>> resources, size_t decompression_buffer_size,
    BlobfsMetrics* metrics, DecompressorCreatorConnector* decompression_connector) {
  std::vector<std::unique_ptr<PageLoader::Worker>> workers;
  ZX_ASSERT(!resources.empty());
  for (auto& res : resources) {
    auto worker_or = PageLoader::Worker::Create(std::move(res), decompression_buffer_size, metrics,
                                                decompression_connector);
    if (worker_or.is_error()) {
      return worker_or.take_error();
    }
    workers.push_back(std::move(worker_or.value()));
  }

  auto pager = std::unique_ptr<PageLoader>(new PageLoader(std::move(workers)));

  // Initialize and start the watchdog.
  pager->watchdog_ = fs_watchdog::CreateWatchdog();
  zx::result<> watchdog_status = pager->watchdog_->Start();
  if (!watchdog_status.is_ok()) {
    FX_LOGS(ERROR) << "Could not start pager watchdog";
    return zx::error(watchdog_status.status_value());
  }

  return zx::ok(std::move(pager));
}

uint32_t PageLoader::AllocateWorker() {
  std::lock_guard l(worker_allocation_lock_);
  ZX_DEBUG_ASSERT(worker_id_allocator_ < workers_.size());
  return worker_id_allocator_++;
}

PagerErrorStatus PageLoader::TransferPages(const PageSupplier& page_supplier, uint64_t offset,
                                           uint64_t length, const LoaderInfo& info) {
  static const fs_watchdog::FsOperationType kOperation(
      fs_watchdog::FsOperationType::CommonFsOperation::PageFault, std::chrono::seconds(60));
  [[maybe_unused]] fs_watchdog::FsOperationTracker tracker(&kOperation, watchdog_.get());

  // Assigns a worker to each pager thread statically.
  thread_local uint32_t worker_id = AllocateWorker();
  return workers_[worker_id]->TransferPages(page_supplier, offset, length, info);
}

}  // namespace blobfs

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/pager/user-pager.h"

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

#include <blobfs/format.h>
#include <fbl/auto_call.h>
#include <fs/trace.h>

#include "src/storage/blobfs/metrics.h"
#include "src/storage/lib/watchdog/include/lib/watchdog/operations.h"

namespace blobfs {
namespace pager {

UserPager::UserPager(size_t decompression_buffer_size, BlobfsMetrics* metrics)
    : decompression_buffer_size_(decompression_buffer_size), metrics_(metrics) {}

zx::status<std::unique_ptr<UserPager>> UserPager::Create(
    std::unique_ptr<TransferBuffer> uncompressed_buffer,
    std::unique_ptr<TransferBuffer> compressed_buffer, size_t decompression_buffer_size,
    BlobfsMetrics* metrics, bool sandbox_decompression) {
  ZX_DEBUG_ASSERT(metrics != nullptr && uncompressed_buffer != nullptr &&
                  uncompressed_buffer->vmo().is_valid() && compressed_buffer != nullptr &&
                  compressed_buffer->vmo().is_valid());

  if (uncompressed_buffer->size() % kBlobfsBlockSize ||
      compressed_buffer->size() % kBlobfsBlockSize ||
      decompression_buffer_size % kBlobfsBlockSize) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (compressed_buffer->size() < decompression_buffer_size) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  TRACE_DURATION("blobfs", "UserPager::Create");

  auto pager = std::unique_ptr<UserPager>(new UserPager(decompression_buffer_size, metrics));
  pager->uncompressed_transfer_buffer_ = std::move(uncompressed_buffer);
  pager->compressed_transfer_buffer_ = std::move(compressed_buffer);

  zx_status_t status =
      pager->compressed_mapper_.Map(pager->compressed_transfer_buffer_->vmo(), 0,
                                    pager->compressed_transfer_buffer_->size(), ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map the compressed TransferBuffer: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }

  status = zx::vmo::create(pager->decompression_buffer_size_, 0, &pager->decompression_buffer_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create decompression buffer: " << zx_status_get_string(status);
    return zx::error(status);
  }

  if (sandbox_decompression) {
    status = zx::vmo::create(kDecompressionBufferSize, 0, &pager->sandbox_buffer_);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "blobfs: Failed to create sandbox buffer: %s\n"
                     << zx_status_get_string(status);
      return zx::error(status);
    }

    auto client_or = ExternalDecompressorClient::Create(pager->sandbox_buffer_,
                                                        pager->compressed_transfer_buffer_->vmo());
    if (!client_or.is_ok()) {
      return zx::error(client_or.status_value());
    }
    pager->decompressor_client_ = std::move(client_or.value());
  }

  // Create the pager object.
  status = zx::pager::create(0, &pager->pager_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize pager";
    return zx::error(status);
  }

  // Start the pager thread.
  thrd_t thread;
  status = pager->pager_loop_.StartThread("blobfs-pager-thread", &thread);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not start pager thread";
    return zx::error(status);
  }

  // Set a scheduling deadline profile for the blobfs-pager-thread. This is purely a performance
  // optimization, and failure to do so is not fatal. So in the case of an error encountered
  // in any of the steps within |SetDeadlineProfile|, we log a warning, and successfully return the
  // UserPager instance.
  SetDeadlineProfile(thread);

  // Initialize and start the watchdog.
  pager->watchdog_ = fs_watchdog::CreateWatchdog();
  zx::status<> watchdog_status = pager->watchdog_->Start();
  if (!watchdog_status.is_ok()) {
    FX_LOGS(ERROR) << "Could not start pager watchdog";
    return zx::error(watchdog_status.status_value());
  }

  return zx::ok(std::move(pager));
}

void UserPager::SetDeadlineProfile(thrd_t thread) {
  zx::channel channel0, channel1;
  zx_status_t status = zx::channel::create(0u, &channel0, &channel1);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Could not create channel pair: " << zx_status_get_string(status);
    return;
  }

  // Connect to the scheduler profile provider service.
  status = fdio_service_connect(
      (std::string("/svc_blobfs/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
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
  } else {
    auto pager_thread = zx::unowned_thread(thrd_get_zx_handle(thread));
    // Set the deadline profile.
    status = pager_thread->set_profile(profile, 0);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to set deadline profile: " << zx_status_get_string(status);
    }
  }
}

UserPager::ReadRange UserPager::GetBlockAlignedReadRange(const UserPagerInfo& info, uint64_t offset,
                                                         uint64_t length) const {
  ZX_DEBUG_ASSERT(offset < info.data_length_bytes);
  // Clamp the range to the size of the blob.
  length = std::min(length, info.data_length_bytes - offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  zx_status_t status = info.verifier->Align(&offset, &length);
  // This only happens if the info.verifier thinks that [offset,length) is out of range, which
  // will only happen if |verifier| was initialized with a different length than the rest of |info|
  // (which is a programming error).
  ZX_DEBUG_ASSERT(status == ZX_OK);

  ZX_DEBUG_ASSERT(offset % kBlobfsBlockSize == 0);
  ZX_DEBUG_ASSERT(length % kBlobfsBlockSize == 0 || offset + length == info.data_length_bytes);

  return {.offset = offset, .length = length};
}

UserPager::ReadRange UserPager::GetBlockAlignedExtendedRange(const UserPagerInfo& info,
                                                             uint64_t offset,
                                                             uint64_t length) const {
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
  constexpr uint64_t kReadAheadClusterSize = (32 * (1 << 10));

  size_t read_ahead_offset = offset;
  size_t read_ahead_length = std::max(kReadAheadClusterSize, length);
  read_ahead_length = std::min(read_ahead_length, info.data_length_bytes - read_ahead_offset);

  // Align to the block size for verification. (In practice this means alignment to 8k).
  return GetBlockAlignedReadRange(info, read_ahead_offset, read_ahead_length);
}

PagerErrorStatus UserPager::TransferPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                               const UserPagerInfo& info) {
  size_t end;
  if (add_overflow(offset, length, &end)) {
    FX_LOGS(ERROR) << "pager transfer range would overflow (off=" << offset << ", len=" << length
                   << ")";
    return PagerErrorStatus::kErrBadState;
  }

  static const fs_watchdog::FsOperationType kOperation(
      fs_watchdog::FsOperationType::CommonFsOperation::PageFault, std::chrono::seconds(60));
  [[maybe_unused]] fs_watchdog::FsOperationTracker tracker(&kOperation, watchdog_.get());

  if (info.decompressor != nullptr) {
    return TransferChunkedPagesToVmo(offset, length, vmo, info);
  } else {
    return TransferUncompressedPagesToVmo(offset, length, vmo, info);
  }
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
PagerErrorStatus UserPager::TransferUncompressedPagesToVmo(uint64_t requested_offset,
                                                           uint64_t requested_length,
                                                           const zx::vmo& vmo,
                                                           const UserPagerInfo& info) {
  ZX_DEBUG_ASSERT(!info.decompressor);

  const auto [start_offset, total_length] =
      GetBlockAlignedExtendedRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "UserPager::TransferUncompressedPagesToVmo", "offset", start_offset,
                 "length", total_length);

  uint64_t offset = start_offset;
  uint64_t length_remaining = total_length;
  uint64_t length;

  // Read in multiples of the transfer buffer size. In practice we should only require one iteration
  // for the majority of cases, since the transfer buffer is 256MB.
  while (length_remaining > 0) {
    length = std::min(uncompressed_transfer_buffer_->size(), length_remaining);

    auto decommit = fit::defer([this, length = length]() {
      // Decommit pages in the transfer buffer that might have been populated. All blobs share the
      // same transfer buffer - this prevents data leaks between different blobs.
      uncompressed_transfer_buffer_->vmo().op_range(
          ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize), nullptr, 0);
    });

    // Read from storage into the transfer buffer.
    auto populate_status = uncompressed_transfer_buffer_->Populate(offset, length, info);
    if (!populate_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferUncompressed: Failed to populate transfer vmo: "
                     << populate_status.status_string();
      return ToPagerErrorStatus(populate_status.status_value());
    }

    const uint64_t rounded_length = fbl::round_up<uint64_t, uint64_t>(length, PAGE_SIZE);

    // The block size is a multiple of the page size and |length| has already been block aligned. If
    // |rounded_length| is greater than |length| then |length| isn't block aligned because it's at
    // the end of the blob.  In the compact layout the Merkle tree can share the last block of the
    // data and may have been read into the transfer buffer.  The Merkle tree needs to be removed
    // before transfering the pages to the destination VMO.
    static_assert(kBlobfsBlockSize % PAGE_SIZE == 0);
    if (rounded_length > length) {
      zx_status_t status = uncompressed_transfer_buffer_->vmo().op_range(
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
          mapping.Map(uncompressed_transfer_buffer_->vmo(), 0, rounded_length, ZX_VM_PERM_READ);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "TransferUncompressed: Failed to map transfer buffer: "
                       << zx_status_get_string(status);
        return ToPagerErrorStatus(status);
      }

      status = info.verifier->VerifyPartial(mapping.start(), length, offset, rounded_length);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "TransferUncompressed: Failed to verify data: "
                       << zx_status_get_string(status);
        return ToPagerErrorStatus(status);
      }
    }

    ZX_DEBUG_ASSERT(offset % PAGE_SIZE == 0);
    // Move the pages from the transfer buffer to the destination VMO.
    zx_status_t status =
        pager_.supply_pages(vmo, offset, rounded_length, uncompressed_transfer_buffer_->vmo(), 0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferUncompressed: Failed to supply pages to paged VMO: "
                     << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
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
PagerErrorStatus UserPager::TransferChunkedPagesToVmo(uint64_t requested_offset,
                                                      uint64_t requested_length, const zx::vmo& vmo,
                                                      const UserPagerInfo& info) {
  ZX_DEBUG_ASSERT(info.decompressor);

  const auto [offset, length] = GetBlockAlignedReadRange(info, requested_offset, requested_length);

  TRACE_DURATION("blobfs", "UserPager::TransferChunkedPagesToVmo", "offset", offset, "length",
                 length);

  fbl::String merkle_root_hash = info.verifier->digest().ToString();

  size_t current_decompressed_offset = offset;
  size_t desired_decompressed_end = offset + length;

  // Read in multiples of the decompression buffer size. In practice we should only require one
  // iteration for the majority of cases, since the decompression buffer is 256MB.
  while (current_decompressed_offset < desired_decompressed_end) {
    size_t current_decompressed_length = desired_decompressed_end - current_decompressed_offset;
    zx::status<CompressionMapping> mapping_status = info.decompressor->MappingForDecompressedRange(
        current_decompressed_offset, current_decompressed_length, decompression_buffer_size_);

    if (!mapping_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to find range for [" << offset << ", "
                     << current_decompressed_offset + current_decompressed_length
                     << "): " << mapping_status.status_string();
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
      compressed_transfer_buffer_->vmo().op_range(
          ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize), nullptr, 0);
    });

    auto populate_status = compressed_transfer_buffer_->Populate(read_offset, read_len, info);
    if (!populate_status.is_ok()) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to populate transfer vmo: "
                     << populate_status.status_string();
      return ToPagerErrorStatus(populate_status.status_value());
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
                                    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE) != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to map decompress buffer: "
                     << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
    }
    auto unmap_decompression = fit::defer([&]() { decompressed_mapper.Unmap(); });

    fs::Ticker ticker(metrics_->Collecting());
    size_t decompressed_size = mapping.decompressed_length;
    zx_status_t decompress_status;
    if (decompressor_client_) {
      ZX_DEBUG_ASSERT(sandbox_buffer_.is_valid());
      auto decommit_sandbox = fit::defer([this, length = mapping.decompressed_length]() {
        // Decommit pages in the sandbox buffer that might have been populated. All blobs share
        // the same sandbox buffer - this prevents data leaks between different blobs.
        sandbox_buffer_.op_range(ZX_VMO_OP_DECOMMIT, 0, fbl::round_up(length, kBlobfsBlockSize),
                                 nullptr, 0);
      });
      ExternalSeekableDecompressor decompressor(decompressor_client_.get(),
                                                info.decompressor.get());
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
      FX_LOGS(ERROR) << "TransferChunked: Failed to decompress: "
                     << zx_status_get_string(decompress_status);
      return ToPagerErrorStatus(decompress_status);
    }
    metrics_->paged_read_metrics().IncrementDecompression(CompressionAlgorithm::CHUNKED,
                                                          decompressed_size, ticker.End(),
                                                          decompressor_client_ != nullptr);

    // Verify the decompressed pages.
    const uint64_t rounded_length =
        fbl::round_up<uint64_t, uint64_t>(mapping.decompressed_length, PAGE_SIZE);
    zx_status_t status =
        info.verifier->VerifyPartial(decompressed_mapper.start(), mapping.decompressed_length,
                                     mapping.decompressed_offset, rounded_length);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to verify data: " << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
    }

    decompressed_mapper.Unmap();

    // Move the pages from the decompression buffer to the destination VMO.
    status = pager_.supply_pages(vmo, mapping.decompressed_offset, rounded_length,
                                 decompression_buffer_, 0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "TransferChunked: Failed to supply pages to paged VMO: "
                     << zx_status_get_string(status);
      return ToPagerErrorStatus(status);
    }
    metrics_->IncrementPageIn(merkle_root_hash, read_offset, read_len);

    // Advance the required decompressed offset based on how much has already been populated.
    current_decompressed_offset = mapping.decompressed_offset + mapping.decompressed_length;
  }

  return PagerErrorStatus::kOK;
}

}  // namespace pager
}  // namespace blobfs

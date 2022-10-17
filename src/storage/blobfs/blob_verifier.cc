// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_verifier.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <safemath/checked_math.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/blobfs/blob_layout.h"

namespace blobfs {

BlobVerifier::BlobVerifier(digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics)
    : digest_(std::move(digest)), metrics_(std::move(metrics)) {}

zx::result<std::unique_ptr<BlobVerifier>> BlobVerifier::Create(
    digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics,
    cpp20::span<const uint8_t> merkle_data_blocks, const BlobLayout& layout,
    const BlobCorruptionNotifier* notifier) {
  std::unique_ptr<BlobVerifier> verifier(new BlobVerifier(std::move(digest), std::move(metrics)));
  verifier->corruption_notifier_ = notifier;
  verifier->tree_verifier_.SetUseCompactFormat(ShouldUseCompactMerkleTreeFormat(layout.Format()));

  if (zx_status_t status = verifier->tree_verifier_.SetDataLength(layout.FileSize());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set merkle data length: " << zx_status_get_string(status);
    return zx::error(status);
  }

  size_t actual_merkle_length = verifier->tree_verifier_.GetTreeLength();
  if (actual_merkle_length > layout.MerkleTreeSize() ||
      layout.MerkleTreeOffsetWithinBlockOffset() + actual_merkle_length >
          merkle_data_blocks.size()) {
    FX_LOGS(ERROR) << "merkle too small for data";
    return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
  }

  const uint8_t* merkle_tree_data =
      merkle_data_blocks.begin() + layout.MerkleTreeOffsetWithinBlockOffset();
  verifier->merkle_data_ = std::make_unique<uint8_t[]>(actual_merkle_length);
  memcpy(verifier->merkle_data_.get(), merkle_tree_data, actual_merkle_length);

  if (zx_status_t status =
          verifier->tree_verifier_.SetTree(verifier->merkle_data_.get(), actual_merkle_length,
                                           verifier->digest_.get(), verifier->digest_.len());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok(std::move(verifier));
}

zx::result<std::unique_ptr<BlobVerifier>> BlobVerifier::CreateWithoutTree(
    digest::Digest digest, std::shared_ptr<BlobfsMetrics> metrics, size_t data_size,
    const BlobCorruptionNotifier* notifier) {
  std::unique_ptr<BlobVerifier> verifier(new BlobVerifier(std::move(digest), std::move(metrics)));
  verifier->corruption_notifier_ = notifier;

  if (zx_status_t status = verifier->tree_verifier_.SetDataLength(data_size); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set merkle data length: " << zx_status_get_string(status);
    return zx::error(status);
  } else if (verifier->tree_verifier_.GetTreeLength() > 0) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier -- data too big for empty tree";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (zx_status_t status = verifier->tree_verifier_.SetTree(nullptr, 0, verifier->digest_.get(),
                                                            verifier->digest_.len());
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok(std::move(verifier));
}

zx_status_t VerifyTailZeroed(const void* data, size_t data_size, size_t buffer_size) {
  size_t tail;
  if (!safemath::CheckSub(buffer_size, data_size).AssignIfValid(&tail)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (tail == 0) {
    return ZX_OK;
  }
  // Check unaligned part first.
  const uint8_t* u8_ptr = static_cast<const uint8_t*>(data) + data_size;
  while (tail & 7) {
    if (*u8_ptr) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    ++u8_ptr;
    --tail;
  }
  // Check remaining aligned part.
  const uint64_t* u64_ptr = reinterpret_cast<const uint64_t*>(u8_ptr);
  while (tail > 0) {
    if (*u64_ptr) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    ++u64_ptr;
    tail -= 8;
  }
  return ZX_OK;
}

zx_status_t BlobVerifier::Verify(const void* data, size_t data_size, size_t buffer_size) {
  TRACE_DURATION("blobfs", "BlobVerifier::Verify", "data_size", data_size);
  fs::Ticker ticker;

  zx_status_t status;
  {
    std::lock_guard l(verification_lock_);
    status = tree_verifier_.Verify(data, data_size, 0);
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Verify(" << digest_.ToString() << ", " << data_size << ", " << buffer_size
                   << ") failed: " << zx_status_get_string(status);
  } else {
    status = VerifyTailZeroed(data, data_size, buffer_size);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "VerifyTailZeroed(" << digest_.ToString() << ", " << data_size << ", "
                     << buffer_size << ") failed: " << zx_status_get_string(status);
    }
  }
  metrics_->verification_metrics().Increment(data_size, tree_verifier_.GetTreeLength(),
                                             ticker.End());
  if (status == ZX_ERR_IO_DATA_INTEGRITY && corruption_notifier_) {
    // Notify the corruption handler server about the corrupted blob.
    corruption_notifier_->NotifyCorruptBlob(digest_);
  }
  return status;
}

zx_status_t BlobVerifier::VerifyPartial(const void* data, size_t length, size_t data_offset,
                                        size_t buffer_size) {
  TRACE_DURATION("blobfs", "BlobVerifier::VerifyPartial", "length", length, "offset", data_offset);
  fs::Ticker ticker;

  zx_status_t status;
  {
    std::lock_guard l(verification_lock_);
    status = tree_verifier_.Verify(data, length, data_offset);
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "VerifyPartial(" << digest_.ToString() << ", " << data_offset << ", "
                   << length << ", " << buffer_size << ") failed: " << zx_status_get_string(status);
  } else {
    status = VerifyTailZeroed(data, length, buffer_size);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "VerifyTailZeroed(" << digest_.ToString() << ", " << length << ", "
                     << buffer_size << ") failed: " << zx_status_get_string(status);
    }
  }
  metrics_->verification_metrics().Increment(length, tree_verifier_.GetTreeLength(), ticker.End());

  if (status == ZX_ERR_IO_DATA_INTEGRITY && corruption_notifier_) {
    // Notify the corruption handler server about the corrupted blob.
    corruption_notifier_->NotifyCorruptBlob(digest_);
  }
  return status;
}

}  // namespace blobfs

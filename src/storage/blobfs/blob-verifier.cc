// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob-verifier.h"

#include <fuchsia/blobfs/c/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <blobfs/blob-layout.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/trace.h>
#include <safemath/checked_math.h>

namespace blobfs {

BlobVerifier::BlobVerifier(BlobfsMetrics* metrics) : metrics_(metrics) {}

zx_status_t BlobVerifier::Create(digest::Digest digest, BlobfsMetrics* metrics, const void* merkle,
                                 size_t merkle_size, BlobLayoutFormat blob_layout_format,
                                 size_t data_size, const BlobCorruptionNotifier* notifier,
                                 std::unique_ptr<BlobVerifier>* out) {
  std::unique_ptr<BlobVerifier> verifier(new BlobVerifier(metrics));
  verifier->digest_ = std::move(digest);
  verifier->corruption_notifier_ = notifier;
  verifier->tree_verifier_.SetUseCompactFormat(
      ShouldUseCompactMerkleTreeFormat(blob_layout_format));
  zx_status_t status = verifier->tree_verifier_.SetDataLength(data_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set merkle data length: " << zx_status_get_string(status);
    return status;
  }
  size_t actual_merkle_length = verifier->tree_verifier_.GetTreeLength();
  if (actual_merkle_length > merkle_size) {
    FX_LOGS(ERROR) << "merkle too small for data";
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if ((status = verifier->tree_verifier_.SetTree(
           merkle, actual_merkle_length, verifier->digest_.get(), verifier->digest_.len())) !=
      ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier: " << zx_status_get_string(status);
    return status;
  }
  *out = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobVerifier::CreateWithoutTree(digest::Digest digest, BlobfsMetrics* metrics,
                                            size_t data_size,
                                            const BlobCorruptionNotifier* notifier,
                                            std::unique_ptr<BlobVerifier>* out) {
  std::unique_ptr<BlobVerifier> verifier(new BlobVerifier(metrics));
  verifier->digest_ = std::move(digest);
  verifier->corruption_notifier_ = notifier;
  zx_status_t status = verifier->tree_verifier_.SetDataLength(data_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set merkle data length: " << zx_status_get_string(status);
    return status;
  } else if (verifier->tree_verifier_.GetTreeLength() > 0) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier -- data too big for empty tree";
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = verifier->tree_verifier_.SetTree(nullptr, 0, verifier->digest_.get(),
                                                 verifier->digest_.len())) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create merkle verifier: " << zx_status_get_string(status);
    return status;
  }
  *out = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobVerifier::VerifyTailZeroed(const void* data, size_t data_size, size_t buffer_size) {
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
  fs::Ticker ticker(metrics_->Collecting());

  zx_status_t status = tree_verifier_.Verify(data, data_size, 0);
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
    // If there is any error to do this, we should not fail the verify.
    zx_status_t notify_status =
        corruption_notifier_->NotifyCorruptBlob(digest_.get(), digest_.len());
    if (notify_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to notify corruptionHandler for blob: " << digest_.ToString()
                     << " error: " << zx_status_get_string(notify_status);
    }
  }
  return status;
}

zx_status_t BlobVerifier::VerifyPartial(const void* data, size_t length, size_t data_offset,
                                        size_t buffer_size) {
  TRACE_DURATION("blobfs", "BlobVerifier::VerifyPartial", "length", length, "offset", data_offset);
  fs::Ticker ticker(metrics_->Collecting());

  zx_status_t status = tree_verifier_.Verify(data, length, data_offset);
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
    // If there is any error to do this, we should not fail the verify.
    zx_status_t notify_status =
        corruption_notifier_->NotifyCorruptBlob(digest_.get(), digest_.len());
    if (notify_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to notify corruptionHandler for blob: " << digest_.ToString()
                     << " error: " << zx_status_get_string(notify_status);
    }
  }
  return status;
}

}  // namespace blobfs

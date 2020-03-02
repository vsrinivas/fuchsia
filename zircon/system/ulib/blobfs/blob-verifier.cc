// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-verifier.h"

#include <zircon/status.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/trace.h>

namespace blobfs {

BlobVerifier::BlobVerifier(BlobfsMetrics* metrics) : metrics_(metrics) {}

zx_status_t BlobVerifier::Create(digest::Digest digest, BlobfsMetrics* metrics, const void* merkle,
                                 size_t merkle_size, size_t data_size,
                                 std::unique_ptr<BlobVerifier>* out) {
  auto verifier = std::make_unique<BlobVerifier>(metrics);
  verifier->digest_ = std::move(digest);
  zx_status_t status = verifier->tree_verifier_.SetDataLength(data_size);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to set merkle data length: %s\n", zx_status_get_string(status));
    return status;
  }
  size_t actual_merkle_length = verifier->tree_verifier_.GetTreeLength();
  if (actual_merkle_length > merkle_size) {
    FS_TRACE_ERROR("blobfs: merkle too small for data\n");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if ((status = verifier->tree_verifier_.SetTree(merkle, actual_merkle_length,
                                                 verifier->digest_.get(),
                                                 verifier->digest_.len())) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create merkle verifier: %s\n", zx_status_get_string(status));
    return status;
  }

  *out = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobVerifier::CreateWithoutTree(digest::Digest digest, BlobfsMetrics *metrics,
                                            size_t data_size, std::unique_ptr<BlobVerifier>* out) {
  auto verifier = std::make_unique<BlobVerifier>(metrics);
  verifier->digest_ = std::move(digest);
  zx_status_t status = verifier->tree_verifier_.SetDataLength(data_size);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to set merkle data length: %s\n", zx_status_get_string(status));
    return status;
  } else if (verifier->tree_verifier_.GetTreeLength() > 0) {
    FS_TRACE_ERROR("blobfs: Failed to create merkle verifier -- data too big for empty tree");
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = verifier->tree_verifier_.SetTree(nullptr, 0, verifier->digest_.get(),
                                                 verifier->digest_.len())) != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to create merkle verifier: %s\n", zx_status_get_string(status));
    return status;
  }
  *out = std::move(verifier);
  return ZX_OK;
}

zx_status_t BlobVerifier::Verify(const void* data, size_t data_size) {
  TRACE_DURATION("blobfs", "BlobVerifier::Verify");
  fs::Ticker ticker(metrics_->Collecting());

  zx_status_t status = tree_verifier_.Verify(data, data_size, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Verify(%s, %lu) failed: %s\n", digest_.ToString().c_str(), data_size,
                   zx_status_get_string(status));
  }
  metrics_->UpdateMerkleVerify(data_size, tree_verifier_.GetTreeLength(), ticker.End());
  return status;
}

zx_status_t BlobVerifier::VerifyPartial(const void* data, size_t length, size_t data_offset) {
  TRACE_DURATION("blobfs", "BlobVerifier::VerifyPartial");
  fs::Ticker ticker(metrics_->Collecting());

  zx_status_t status = tree_verifier_.Verify(data, length, data_offset);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Verify(%s, %lu, %lu) failed: %s\n", digest_.ToString().c_str(),
                   data_offset, length, zx_status_get_string(status));
  }
  metrics_->UpdateMerkleVerify(length, tree_verifier_.GetTreeLength(), ticker.End());
  return status;
}

}  // namespace blobfs

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-corruption-notifier.h"

#include <fuchsia/blobfs/c/fidl.h>
#include <zircon/status.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fs/trace.h>
#include <safemath/checked_math.h>

namespace blobfs {

zx_status_t BlobCorruptionNotifier::Create(std::unique_ptr<BlobCorruptionNotifier>* out) {
  if (out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::unique_ptr<BlobCorruptionNotifier> notifier(new BlobCorruptionNotifier());
  *out = std::move(notifier);
  return ZX_OK;
}

void BlobCorruptionNotifier::SetCorruptBlobHandler(zx::channel blobfs_handler) {
  corruption_handler_.reset();
  corruption_handler_ = std::move(blobfs_handler);
}

zx_status_t BlobCorruptionNotifier::NotifyCorruptBlob(const uint8_t* blob_root_hash,
                                                      size_t blob_root_len) const {
  if (blob_root_hash == nullptr || blob_root_len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (corruption_handler_.get() == ZX_HANDLE_INVALID) {
    FS_TRACE_WARN("blobfs: Invalid corruption handler\n");
    // If the corruption handler has not been registered yet, we should not error out due to
    // unset corruption handler.
    return ZX_OK;
  }

  FS_TRACE_INFO("blobfs: Notifying corruption handler service\n");
  return fuchsia_blobfs_CorruptBlobHandlerCorruptBlob(corruption_handler_.get(), blob_root_hash,
                                                      blob_root_len);
}

}  // namespace blobfs

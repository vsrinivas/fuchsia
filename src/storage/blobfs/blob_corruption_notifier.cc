// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_corruption_notifier.h"

#include <fuchsia/blobfs/c/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <safemath/checked_math.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"

namespace blobfs {

zx_status_t BlobCorruptionNotifier::Create(std::unique_ptr<BlobCorruptionNotifier>* out) {
  if (out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  std::unique_ptr<BlobCorruptionNotifier> notifier(new BlobCorruptionNotifier());
  *out = std::move(notifier);
  return ZX_OK;
}

void BlobCorruptionNotifier::SetCorruptBlobHandler(
    fidl::ClientEnd<fuchsia_blobfs::CorruptBlobHandler> blobfs_handler) {
  corruption_handler_.reset();
  corruption_handler_ = std::move(blobfs_handler);
}

zx_status_t BlobCorruptionNotifier::NotifyCorruptBlob(const uint8_t* blob_root_hash,
                                                      size_t blob_root_len) const {
  if (blob_root_hash == nullptr || blob_root_len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!corruption_handler_.is_valid()) {
    FX_LOGS(WARNING) << "Invalid corruption handler";
    // If the corruption handler has not been registered yet, we should not error out due to
    // unset corruption handler.
    return ZX_OK;
  }

  FX_LOGS(INFO) << "Notifying corruption handler service";
  auto result = fuchsia_blobfs::CorruptBlobHandler::Call::CorruptBlob(
      corruption_handler_,
      fidl::VectorView<uint8_t>(fidl::unowned_ptr(const_cast<uint8_t*>(blob_root_hash)),
                                blob_root_len));
  return result.status();
}

}  // namespace blobfs

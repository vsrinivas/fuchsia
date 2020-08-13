// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_
#define SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_

#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <digest/digest.h>

namespace blobfs {
using digest::Digest;

// BlobCorruptionNotifier notifies a handler of blob corruption,
// if a handler has been registered.
class BlobCorruptionNotifier {
 public:
  BlobCorruptionNotifier(const BlobCorruptionNotifier&) = delete;
  BlobCorruptionNotifier(BlobCorruptionNotifier&&) = delete;
  BlobCorruptionNotifier& operator=(const BlobCorruptionNotifier&) = delete;
  BlobCorruptionNotifier& operator=(BlobCorruptionNotifier&&) = delete;

  // Creates a single instance of BlobCorruptionNotifier for all blobs.
  static zx_status_t Create(std::unique_ptr<BlobCorruptionNotifier>* out);

  void SetCorruptBlobHandler(zx::channel blobfs_handler);

  // Notifies corrupt blob to the corruption handler service.
  // If handler is not registered, simply ignore notifying and continue.
  zx_status_t NotifyCorruptBlob(const uint8_t* blob_root_hash, size_t blob_root_len) const;

 private:
  BlobCorruptionNotifier() {}
  zx::channel corruption_handler_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_

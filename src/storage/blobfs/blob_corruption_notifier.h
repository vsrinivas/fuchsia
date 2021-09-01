// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_
#define SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_

#include <fidl/fuchsia.blobfs/cpp/wire.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/digest/digest.h"

namespace blobfs {

// BlobCorruptionNotifier notifies a handler of blob corruption, if a handler has been registered.
class BlobCorruptionNotifier {
 public:
  // Notifies corrupt blob to the corruption handler service.
  virtual void NotifyCorruptBlob(const digest::Digest& digest) const = 0;
};

// Implementation of BlobCorruptionNotifier that nofities over a Fidl interface.
class FidlBlobCorruptionNotifier : public BlobCorruptionNotifier {
 public:
  void set_corruption_handler(fidl::ClientEnd<fuchsia_blobfs::CorruptBlobHandler> handler) {
    corruption_handler_ = std::move(handler);
  }

  // BlobCorruptionNotifier implementation:
  void NotifyCorruptBlob(const digest::Digest& digest) const override;

 private:
  // This handler can be null if no Fidl handler is registered.
  fidl::ClientEnd<fuchsia_blobfs::CorruptBlobHandler> corruption_handler_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_CORRUPTION_NOTIFIER_H_

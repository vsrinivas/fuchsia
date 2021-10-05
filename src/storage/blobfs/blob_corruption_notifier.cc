// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_corruption_notifier.h"

#include <fuchsia/blobfs/c/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace blobfs {

void FidlBlobCorruptionNotifier::NotifyCorruptBlob(const digest::Digest& digest) const {
  FX_LOGS(ERROR) << "Corrupt blob: " << digest.ToString();

  if (corruption_handler_.is_valid()) {
    fidl::WireCall(corruption_handler_)
        ->CorruptBlob(fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(digest.get()),
                                                              digest.len()));
  } else {
    // We normally expect the updater system to be registered for corrupted blobs.
    FX_LOGS(INFO) << "No corruption handler registered while processing corrupt blob.";
  }
}

}  // namespace blobfs

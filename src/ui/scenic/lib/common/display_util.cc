// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/common/display_util.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic {
DisplayBufferCollectionId ImportBufferCollection(
    const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
    const fuchsia::hardware::display::ImageConfig& image_config) {
  // This function will be called from multiple threads, and thus needs an atomic
  // incrementor for the id.
  static std::atomic<uint64_t> buffer_collection_id = 0;
  buffer_collection_id++;
  zx_status_t status;

  if (display_controller->ImportBufferCollection(buffer_collection_id, std::move(token), &status) !=
          ZX_OK ||
      status != ZX_OK) {
    FX_LOGS(ERROR) << "ImportBufferCollection failed - status: " << status;
    return 0;
  }

  if (display_controller->SetBufferCollectionConstraints(buffer_collection_id, image_config,
                                                         &status) != ZX_OK ||
      status != ZX_OK) {
    FX_LOGS(ERROR) << "SetBufferCollectionConstraints failed.";

    if (display_controller->ReleaseBufferCollection(buffer_collection_id) != ZX_OK) {
      FX_LOGS(ERROR) << "ReleaseBufferCollection failed.";
    }
    return 0;
  }

  return buffer_collection_id;
}

}  // namespace scenic

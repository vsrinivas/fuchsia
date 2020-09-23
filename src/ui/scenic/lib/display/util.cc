// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

namespace scenic_impl {

DisplayBufferCollectionId GenerateUniqueCollectionId() {
  // This function will be called from multiple threads, and thus needs an atomic
  // incrementor for the id.
  static std::atomic<DisplayBufferCollectionId> buffer_collection_id = 0;
  return ++buffer_collection_id;
}

bool ImportBufferCollection(DisplayBufferCollectionId buffer_collection_id,
                            const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                            fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
                            const fuchsia::hardware::display::ImageConfig& image_config) {
  zx_status_t status;

  if (display_controller->ImportBufferCollection(buffer_collection_id, std::move(token), &status) !=
          ZX_OK ||
      status != ZX_OK) {
    FX_LOGS(ERROR) << "ImportBufferCollection failed - status: " << status;
    return false;
  }

  if (display_controller->SetBufferCollectionConstraints(buffer_collection_id, image_config,
                                                         &status) != ZX_OK ||
      status != ZX_OK) {
    FX_LOGS(ERROR) << "SetBufferCollectionConstraints failed.";

    if (display_controller->ReleaseBufferCollection(buffer_collection_id) != ZX_OK) {
      FX_LOGS(ERROR) << "ReleaseBufferCollection failed.";
    }
    return false;
  }

  return true;
}

DisplayEventId ImportEvent(const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                           const zx::event& event) {
  static DisplayEventId id_generator = fuchsia::hardware::display::INVALID_DISP_ID + 1;

  zx::event dup;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate display controller event.";
    return fuchsia::hardware::display::INVALID_DISP_ID;
  }

  // Generate a new display ID after we've determined the event can be duplicated as to not
  // waste an id.
  DisplayEventId event_id = id_generator++;

  auto before = zx::clock::get_monotonic();
  auto status = display_controller->ImportEvent(std::move(dup), event_id);
  if (status != ZX_OK) {
    auto after = zx::clock::get_monotonic();
    FX_LOGS(ERROR) << "Failed to import display controller event. Waited "
                   << (after - before).to_msecs() << "msecs. Error code: " << status;
    return fuchsia::hardware::display::INVALID_DISP_ID;
  }
  return event_id;
}

}  // namespace scenic_impl

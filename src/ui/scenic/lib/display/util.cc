// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

namespace scenic_impl {

bool ImportBufferCollection(sysmem_util::GlobalBufferCollectionId buffer_collection_id,
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

bool IsCaptureSupported(const fuchsia::hardware::display::ControllerSyncPtr& display_controller) {
  fuchsia::hardware::display::Controller_IsCaptureSupported_Result capture_supported_result;
  auto status = display_controller->IsCaptureSupported(&capture_supported_result);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "IsCaptureSupported status failure: " << status;
    return false;
  }

  if (!capture_supported_result.is_response()) {
    FX_LOGS(ERROR) << "IsCaptureSupported did not return a valid response.";
    return false;
  }

  return capture_supported_result.response().supported;
}

uint64_t ImportImageForCapture(
    const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
    const fuchsia::hardware::display::ImageConfig& image_config,
    sysmem_util::GlobalBufferCollectionId buffer_collection_id, uint64_t vmo_idx) {
  if (buffer_collection_id == 0) {
    FX_LOGS(ERROR) << "Buffer collection id is 0.";
    return 0;
  }

  if (image_config.type != fuchsia::hardware::display::TYPE_CAPTURE) {
    FX_LOGS(ERROR) << "Image config type must be TYPE_CAPTURE.";
    return 0;
  }

  fuchsia::hardware::display::Controller_ImportImageForCapture_Result import_result;
  auto status = display_controller->ImportImageForCapture(image_config, buffer_collection_id,
                                                          vmo_idx, &import_result);

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL transport error, status: " << status;
    return 0;
  } else if (import_result.is_err()) {
    FX_LOGS(ERROR) << "FIDL server error response: " << import_result.err();
    return 0;
  } else {
    return import_result.response().image_id;
  }
}

}  // namespace scenic_impl

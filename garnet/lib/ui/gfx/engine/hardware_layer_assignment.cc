// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/hardware_layer_assignment.h"

#include <unordered_set>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

bool HardwareLayerAssignment::IsValid() {
  if (!swapchain) {
    FXL_LOG(WARNING) << "Invalid HardwareLayerAssignment: no swapchain.";
    return false;
  } else if (items.empty()) {
    FXL_LOG(WARNING) << "Invalid HardwareLayerAssignment: no items.";
    return false;
  }

  std::unordered_set<uint8_t> layer_ids;
  for (auto& item : items) {
    if (item.layers.empty()) {
      FXL_LOG(WARNING)
          << "Invalid HardwareLayerAssignment: item with no layers.";
      return false;
    }
    auto result = layer_ids.insert(item.hardware_layer_id);
    if (!result.second) {
      FXL_LOG(WARNING) << "Invalid HardwareLayerAssignment: duplicate layer ID "
                       << item.hardware_layer_id;
      return false;
    }
  }

  // Valid!
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl

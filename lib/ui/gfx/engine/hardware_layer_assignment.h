// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_HARDWARE_LAYER_ASSIGNMENT_H_
#define GARNET_LIB_UI_GFX_ENGINE_HARDWARE_LAYER_ASSIGNMENT_H_

#include <vector>

namespace scenic_impl {
namespace gfx {

class Layer;
class Swapchain;

// Each item has a list of Layers that should be rendered and GPU-composited
// into a single image, which should then be displayed on the hardware layer
// specified by |hardware_layer_id|.  This ID must be unique within each
// HardwareLayerAssignment struct: no two Items can share the same ID.
struct HardwareLayerAssignment {
  // Each item is guaranteed to have a non-zero number of layers.
  struct Item {
    uint8_t hardware_layer_id;
    std::vector<Layer*> layers;
  };

  std::vector<Item> items;
  Swapchain* swapchain = nullptr;

  explicit operator bool() const { return swapchain && !items.empty(); }
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_HARDWARE_LAYER_ASSIGNMENT_H_

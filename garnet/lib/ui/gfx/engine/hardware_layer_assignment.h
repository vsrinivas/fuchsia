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
// specified by |hardware_layer_id|.  See IsValid() for a list of validity
// requirements for this struct.
struct HardwareLayerAssignment {
  // Each item is guaranteed to have a non-zero number of layers.
  struct Item {
    uint8_t hardware_layer_id;
    std::vector<Layer*> layers;
  };

  std::vector<Item> items;
  Swapchain* swapchain = nullptr;

  // For a HardwareLayerAssigment to be valid, it must:
  // - have a non-null swapchain
  // - have at least one Item
  // - each Item must have a non-empty list of Layers.
  // - no two items can have the same hardware_layer_id.
  bool IsValid();
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_HARDWARE_LAYER_ASSIGNMENT_H_

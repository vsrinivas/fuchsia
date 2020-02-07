// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/vk/pipeline_layout.h"

namespace escher {
namespace impl {

PipelineLayout::PipelineLayout(vk::Device device, vk::PipelineLayout layout)
    : device_(device), layout_(layout) {
  FXL_DCHECK(layout);
}

PipelineLayout::~PipelineLayout() {
  // Not specifying a device allows unit-testing without calling Vulkan APIs.
  if (device_) {
    device_.destroyPipelineLayout(layout_);
  }
}

}  // namespace impl
}  // namespace escher

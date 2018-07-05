// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/vk/pipeline.h"

namespace escher {
namespace impl {

Pipeline::Pipeline(vk::Device device, vk::Pipeline pipeline,
                   PipelineLayoutPtr layout, PipelineSpec spec)
    : device_(device),
      pipeline_(pipeline),
      layout_(std::move(layout)),
      spec_(std::move(spec)) {
  FXL_DCHECK(pipeline_);
  FXL_DCHECK(layout_);
}

Pipeline::~Pipeline() {
  // Not specifying a device allows unit-testing without calling Vulkan APIs.
  if (device_) {
    device_.destroyPipeline(pipeline_);
  }
}

}  // namespace impl
}  // namespace escher

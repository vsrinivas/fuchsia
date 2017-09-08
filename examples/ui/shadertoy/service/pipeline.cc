// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/pipeline.h"

namespace shadertoy {

Pipeline::Pipeline(vk::Device device,
                   vk::Pipeline pipeline,
                   vk::PipelineLayout pipeline_layout)
    : device_(device), pipeline_(pipeline), pipeline_layout_(pipeline_layout) {}

Pipeline::~Pipeline() {
  device_.destroyPipeline(pipeline_);
  device_.destroyPipelineLayout(pipeline_layout_);
}

}  // namespace shadertoy

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/pipeline.h"

#include "ftl/logging.h"

namespace escher {
namespace impl {

Pipeline::Pipeline(const PipelineSpec& spec,
                   vk::Device device,
                   vk::Pipeline pipeline,
                   vk::PipelineLayout pipeline_layout)
    : spec_(spec),
      device_(device),
      pipeline_(pipeline),
      pipeline_layout_(pipeline_layout) {}

Pipeline::~Pipeline() {
  // TODO: must change this to share layouts between pipelines.
  device_.destroyPipeline(pipeline_);
  device_.destroyPipelineLayout(pipeline_layout_);
}

}  // namespace impl
}  // namespace escher

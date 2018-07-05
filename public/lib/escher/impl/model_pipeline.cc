// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_pipeline.h"

#include "lib/fxl/logging.h"

namespace escher {
namespace impl {

ModelPipeline::ModelPipeline(const ModelPipelineSpec& spec, vk::Device device,
                             vk::Pipeline pipeline,
                             vk::PipelineLayout pipeline_layout)
    : spec_(spec),
      device_(device),
      pipeline_(pipeline),
      pipeline_layout_(pipeline_layout) {}

ModelPipeline::~ModelPipeline() {
  // TODO: must change this to share layouts between pipelines.
  device_.destroyPipeline(pipeline_);
  device_.destroyPipelineLayout(pipeline_layout_);
}

}  // namespace impl
}  // namespace escher

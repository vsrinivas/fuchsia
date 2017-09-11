// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "lib/fxl/memory/ref_counted.h"

namespace shadertoy {

class Pipeline;
using PipelinePtr = fxl::RefPtr<Pipeline>;

class Pipeline : public fxl::RefCountedThreadSafe<Pipeline> {
 public:
  explicit Pipeline(vk::Device device,
                    vk::Pipeline pipeline,
                    vk::PipelineLayout layout);
  ~Pipeline();

  vk::Pipeline vk_pipeline() const { return pipeline_; }
  vk::PipelineLayout vk_pipeline_layout() const { return pipeline_layout_; }

 private:
  vk::Device device_;
  vk::Pipeline pipeline_;
  vk::PipelineLayout pipeline_layout_;
};

}  // namespace shadertoy

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/impl/pipeline_spec.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class Pipeline {
 public:
  Pipeline(const PipelineSpec& spec,
           vk::Device device,
           vk::Pipeline pipeline,
           vk::PipelineLayout pipeline_layout);
  ~Pipeline();

  vk::Pipeline pipeline() const { return pipeline_; }
  vk::PipelineLayout pipeline_layout() const { return pipeline_layout_; }

 private:
  friend class PipelineCache;

  PipelineSpec spec_;
  vk::Device device_;
  vk::Pipeline pipeline_;
  vk::PipelineLayout pipeline_layout_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Pipeline);
};

}  // namespace impl
}  // namespace escher

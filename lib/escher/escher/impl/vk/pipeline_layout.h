// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "lib/ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Manages the lifecycle of a Vulkan PipelineLayout.
class PipelineLayout : public ftl::RefCountedThreadSafe<PipelineLayout> {
 public:
  // The vk::PipelineLayout becomes owned by this PipelineLayout instance, and
  // is destroyed.  The vk::Device is not owned; it is used for destroying
  // the pipeline layout.
  PipelineLayout(vk::Device device, vk::PipelineLayout layout);
  ~PipelineLayout();

  vk::PipelineLayout get() const { return layout_; }

 private:
  vk::Device device_;
  vk::PipelineLayout layout_;
};

typedef ftl::RefPtr<PipelineLayout> PipelineLayoutPtr;

}  // namespace impl
}  // namespace escher

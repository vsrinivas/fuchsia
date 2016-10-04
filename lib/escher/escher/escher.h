// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include "ftl/macros.h"

namespace escher {
namespace impl {
class EscherImpl;
}

class Model;
class Stage;
struct VulkanContext;
struct VulkanSwapchain;

enum class Status {
  kOk,
  kNotReady,
  kTimeout,
  kOutOfHostMemory,
  kOutOfDeviceMemory,
  kDeviceLost,
  kInternalError  // should not occur
};

// Escher is the primary class used by clients of the Escher library.
class Escher {
 public:
  // Escher does not take ownership of the objects in the Vulkan context.  It is
  // up to the application to eventually destroy them, and also to ensure that
  // they outlive the Escher instance.
  Escher(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~Escher();

  // Render a frame.
  Status Render(const Stage& stage, const Model& model);

  // Notify Escher that the swapchain has changed.
  void SetSwapchain(const VulkanSwapchain& swapchain);

 private:
  std::unique_ptr<impl::EscherImpl> impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Escher);
};

}  // namespace escher

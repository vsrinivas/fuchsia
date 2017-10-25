// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/swapchain/swapchain.h"

#include <zx/event.h>
#include <zx/handle.h>
#include <zx/vmo.h>
#include <vulkan/vulkan.hpp>

#include "garnet/bin/ui/scene_manager/swapchain/magma_buffer.h"
#include "garnet/bin/ui/scene_manager/swapchain/magma_connection.h"
#include "garnet/bin/ui/scene_manager/swapchain/magma_semaphore.h"
#include "garnet/bin/ui/scene_manager/sync/fence_listener.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scene_manager {

class Display;
class EventTimestamper;

// DisplaySwapchain implements the Swapchain interface by using a Vulkan
// swapchain to present images to a physical display using the Magma API.
class DisplaySwapchain : public Swapchain {
 public:
  DisplaySwapchain(Display* display,
                   EventTimestamper* timestamper,
                   escher::Escher* escher);
  ~DisplaySwapchain() override;

  // |Swapchain|
  bool DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                           DrawCallback draw_callback) override;

 private:
  struct Framebuffer {
    zx::vmo vmo;
    escher::GpuMemPtr device_memory;
    escher::ImagePtr escher_image;
    MagmaBuffer magma_buffer;
  };

  struct Semaphore {
    std::unique_ptr<FenceListener> fence;
    escher::SemaphorePtr escher_semaphore;
    MagmaSemaphore magma_semaphore;
  };

  bool InitializeFramebuffers(escher::ResourceRecycler* resource_recycler);

  Semaphore Export(escher::SemaphorePtr escher_semaphore);

  Display* const display_;
  EventTimestamper* const event_timestamper_;
  MagmaConnection magma_connection_;

  vk::Format format_;
  vk::Device device_;
  vk::Queue queue_;

  const escher::VulkanDeviceQueues::ProcAddrs& vulkan_proc_addresses_;

  size_t next_semaphore_index_ = 0;

  std::vector<Framebuffer> swapchain_buffers_;
  std::vector<Semaphore> image_available_semaphores_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplaySwapchain);
};

}  // namespace scene_manager

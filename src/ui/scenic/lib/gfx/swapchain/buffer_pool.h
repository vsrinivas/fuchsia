// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_BUFFER_POOL_H_
#define SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_BUFFER_POOL_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <zircon/pixelformat.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_listener.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {

class BufferPool {
 public:
  struct Framebuffer {
    zx::vmo vmo;
    escher::GpuMemPtr device_memory;
    escher::ImagePtr escher_image;
    uint64_t id;
  };

  struct Environment {
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller = nullptr;
    display::Display* display = nullptr;
    escher::Escher* escher = nullptr;
    Sysmem* sysmem = nullptr;
    escher::ResourceRecycler* recycler = nullptr;
    vk::Device vk_device;
  };

  // Creates a pool of |capacity| buffers for use in |env|. |env| is not retained.
  BufferPool(size_t count, Environment* environment, bool use_protected_memory);
  ~BufferPool();

  BufferPool& operator=(BufferPool&& rhs);

  // Destroys all buffers. The pool is no longer usable after this.
  void Clear(std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller);

  // Gets an unused buffer or returns null.
  Framebuffer* GetUnused();

  // Puts an acquired buffer back into the pool.
  void Put(Framebuffer* f);

  const fuchsia::hardware::display::ImageConfig& image_config() { return image_config_; }
  vk::Format image_format() { return image_format_; }
  bool empty() const { return buffers_.empty(); }
  size_t size() const { return buffers_.size(); }

 private:
  bool CreateBuffers(size_t count, Environment* environment, bool use_protected_memory);
  std::vector<Framebuffer> buffers_;
  std::vector<bool> used_;
  fuchsia::hardware::display::ImageConfig image_config_;
  vk::Format image_format_ = vk::Format::eB8G8R8A8Unorm;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferPool);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_BUFFER_POOL_H_

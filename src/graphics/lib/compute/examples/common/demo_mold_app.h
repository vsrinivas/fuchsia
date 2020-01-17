// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_MOLD_APP_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_MOLD_APP_H_

#include <vector>

#include "demo_spinel_image.h"
#include "demo_vulkan_app.h"
#include "spinel/spinel_types.h"
#include "tests/common/scoped_struct.h"
#include "tests/common/vk_buffer.h"

// Base class for all demos that render things using Mold in a Vulkan window.
// Usage is the following:
//
//   1) Create new instance, providing configuration information, as well
//      as a DemoSpinelImageProvider instance, that will be used by the
//      class to retrieve Spinel image content for each rendered frame.
//
//   2) REQUIRED: Call setImageProvider() to specify the demo image provider,
//      which determines how each frame is rendered with the Spinel API.
//
//   3) Call run().
//
class DemoMoldApp : public DemoVulkanApp {
 public:
  struct Config
  {
    DemoVulkanApp::Config app;
    bool                  no_clear = false;
  };

  DemoMoldApp(const Config & config);

  virtual ~DemoMoldApp();

  void
  setImageProvider(std::unique_ptr<DemoSpinelImageProvider> image_provider)
  {
    image_provider_ = std::move(image_provider);
  }

 protected:
  bool
  setup() override;

  void
  teardown() override;

  bool
  drawFrame(uint32_t frame_counter) override;

  // Used to implement ScopedBuffer below, i.e. ScopedStruct<vk_buffer_t> easily.
  struct vk_buffer_traits_t
  {
    static constexpr vk_buffer_t kDefault = {};

    template <typename... ARGS>
    static void
    init(vk_buffer_t * buffer, ARGS... args) noexcept
    {
      vk_buffer_alloc_host(buffer, std::forward<ARGS>(args)...);
    }
    static void
    destroy(vk_buffer_t * buffer) noexcept
    {
      if (buffer->buffer)
        vk_buffer_free(buffer);
    }
    static void
    move(vk_buffer_t * to, vk_buffer_t * from) noexcept
    {
      *to   = *from;
      *from = (vk_buffer_t){};
    }
  };

  using ScopedBuffer = ScopedStruct<vk_buffer_t, vk_buffer_traits_t>;

  bool                                     config_no_clear_     = false;
  bool                                     config_reset_before_ = false;
  std::unique_ptr<DemoSpinelImageProvider> image_provider_;

  spn_context_t             spinel_context_;
  std::vector<ScopedBuffer> image_buffers_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_MOLD_APP_H_

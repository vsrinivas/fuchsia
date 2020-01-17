// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_APP_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_APP_H_

#include <memory>
#include <vector>

#include "demo_spinel_image.h"
#include "demo_vulkan_app.h"
#include "spinel/spinel_types.h"
#include "spinel/spinel_vk_types.h"
#include "tests/common/spinel_vk/spinel_vk_submit_state.h"

// Base class for all demos that render things using Spinel on Vulkan.
// Usage is the following:
//
//   1) Create new instance, providing configuration information.
//
//   2) REQUIRED: Call setImageProvider() to specify the image provider
//      that will provide Spinel images to display and their transforms
//      for each frame.
//
//   2) Call run().
//
class DemoSpinelApp : public DemoVulkanApp {
 public:
  struct Config
  {
    DemoVulkanApp::Config app;
    uint32_t              wanted_vendor_id = 0;
    uint32_t              wanted_device_id = 0;
    bool                  no_clear         = false;
  };

  DemoSpinelApp(const Config & config);

  virtual ~DemoSpinelApp();

  // Set the DemoSpinelImageProvider to be used to render images
  // into swapchain images with the Spinel library.
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

  bool                                     config_no_clear_ = false;
  std::unique_ptr<DemoSpinelImageProvider> image_provider_  = nullptr;

  struct spn_vk_environment spinel_env_;
  spn_context_t             spinel_context_;
  VkSampler                 surface_sampler_;
  uint32_t                  frame_index_ = 0;

  std::vector<SpinelVkSubmitState> spinel_submits_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_SPINEL_APP_H_

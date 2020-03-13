// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_APP_SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_APP_SPINEL_H_

#include <memory>
#include <vector>

#include "demo_app_base.h"
#include "demo_image_group.h"
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
class DemoAppSpinel : public DemoAppBase {
 public:
  struct Config
  {
    DemoAppBase::Config app;
    uint32_t            wanted_vendor_id = 0;
    uint32_t            wanted_device_id = 0;
    bool                no_clear         = false;
  };

  DemoAppSpinel(const Config & config);

  virtual ~DemoAppSpinel();

  // Set the DemoImageGroup to be used to render images
  // into swapchain images with the Spinel library.
  void
  setImageFactory(const DemoImage::Factory & factory)
  {
    demo_images_.setFactory(std::move(factory));
  }

 protected:
  bool
  setup() override;

  void
  teardown() override;

  bool
  drawFrame(uint32_t frame_counter) override;

  bool           config_no_clear_ = false;
  DemoImageGroup demo_images_;

  struct spn_vk_environment spinel_env_;
  spn_context_t             spinel_context_;
  VkSampler                 surface_sampler_;

  std::vector<SpinelVkSubmitState> spinel_submits_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_APP_SPINEL_H_

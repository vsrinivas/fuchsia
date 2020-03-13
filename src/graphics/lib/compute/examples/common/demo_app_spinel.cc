// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_app_spinel.h"

#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_vk.h"
#include "tests/common/spinel_vk/spinel_vk_device_config_utils.h"
#include "tests/common/spinel_vk/spinel_vk_submit_state.h"
#include "tests/common/utils.h"
#include "tests/common/vk_sampler.h"

// Set to 1 to enable log messages during development.
#define ENABLE_LOG 0

#if ENABLE_LOG
#include <stdio.h>
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

DemoAppSpinel::DemoAppSpinel(const Config & config) : config_no_clear_(config.no_clear)
{
  LOG("CREATING VULKAN DEVICE AND PRESENTATION SURFACE\n");
  VulkanDevice::Config dev_config = {
    .app_name          = config.app.app_name,
    .verbose           = config.app.verbose,
    .debug             = config.app.debug,
    .require_swapchain = true,
    .disable_vsync     = config.app.disable_vsync,
  };

  struct spn_vk_context_create_info spinel_create_info = {};
  ASSERT_MSG(device_.initForSpinel(dev_config,
                                   config.wanted_vendor_id,
                                   config.wanted_device_id,
                                   &spinel_create_info),
             "Could not initialize Vulkan device for Spinel!\n");

  DemoAppBase::Config app_config                    = config.app;
  app_config.require_swapchain_image_shader_storage = true;

  ASSERT_MSG(this->DemoAppBase::initAfterDevice(app_config), "Could not initialize application!\n");

  spinel_env_ = vk_app_state_get_spinel_environment(&device_.vk_app_state());

  spn(vk_context_create(&spinel_env_, &spinel_create_info, &spinel_context_));

  surface_sampler_ =
    vk_sampler_create_linear_clamp_to_edge(device_.vk_device(), device_.vk_allocator());
  LOG("INIT COMPLETED\n");
}

DemoAppSpinel::~DemoAppSpinel()
{
  LOG("DESTRUCTOR\n");
  spn_context_release(spinel_context_);
  LOG("DESTRUCTOR COMPLETED\n");
}

//
//
//
bool
DemoAppSpinel::setup()
{
  LOG("SETUP\n");
  demo_images_.setup((DemoImage::Config){
    .context        = spinel_context_,
    .surface_width  = window_extent().width,
    .surface_height = window_extent().height,
    .image_count    = swapchain_image_count_,
  });

  // Create one SpinelVkSubmitState per swapchain image.
  spinel_submits_.resize(swapchain_image_count_);
  memset(spinel_submits_.data(), 0, sizeof(spinel_submits_[0]) * spinel_submits_.size());

  LOG("SETUP COMPLETED\n");
  return true;
}

//
//
//
void
DemoAppSpinel::teardown()
{
  LOG("TEARDOWN\n");

  // Need to force submission of previous frame. See tech note in drawFrame().
  demo_images_.getPreviousImage().flush();

  // Force spinel to complete rendering operations, to trigger swapchain
  // presentation of the last acquired swapchain image.
  spn_vk_context_wait(spinel_context_, 0, NULL, true, UINT64_MAX, NULL);

  spinel_submits_.clear();
  demo_images_.teardown();

  this->DemoAppBase::teardown();
  LOG("TEARDOWN COMPLETED\n");
}

//
//
//

bool
DemoAppSpinel::drawFrame(uint32_t frame_counter)
{
  LOG("FRAME %u\n", frame_counter);

  // Technical note:
  //
  // While this may change in the future, rendering with Spinel currently
  // works as follows:
  //
  //   1) Prepare the composition and styling to be rendered, and seal them.
  //
  //   2) Call spn_render() with the composition and styling as argument.
  //      This also takes a chain of submit extensions (managed by a
  //      SpinelVkSubmitState in this program).
  //
  //      Note that spn_render() doesn't necessarily render anything or submit
  //      work to the GPU. Instead it prepares a command buffer for eventual
  //      submission.
  //
  //   3) Unsealing the composition or the styling of a previous spn_render()
  //      call will also submit a pending command buffer to the Vulkan
  //      compute queue (note: unsealing a composition or styling that
  //      was not sent to spn_render() is a no-op).
  //
  //      If the submit extensions chain specifies a user-provided callback
  //      (see spn_vk_render_submit_ext_image_render and
  //      spn_vk_render_submit_ext_image_render_pfn_t), the latter will be
  //      called to perform this submit (otherwise Spinel does the submit
  //      itself).
  //
  //      A user-provided callback is a useful way to synchronize Spinel
  //      rendering operations with other Vulkan ones, like swapchain image
  //      acquisition and presentation.
  //
  // The simplest way to render images to a swapchain thus looks like:
  //
  //    For each frame:
  //
  //       1) Call acquireSwapchainImage()
  //
  //       2) Setup the image's composition and styling
  //
  //       3) Call spn_render(), using a custom callback that will setup a
  //          VkSubmitInfo that waits for the image-acquired semaphore, and
  //          signals the image-rendered semaphore.
  //
  //       4) Unseal the composition and styling, to force the submit through
  //          the custom callback.
  //
  //       5) Call presentSwapchainImage().
  //
  // However, it is possible to achieve better performance by using multiple
  // Spinel images and overlapping their setup and presentation as follows:
  //
  //   For each frame:
  //
  //       1) Unseal the composition and styling of the previously rendered
  //          frame. Due to step 3) below, this will force its submission and
  //          ask for its presentation.
  //
  //       2) Call acquireSwapchainImage()
  //
  //       3) Setup a new image's composition and styling and seal them.
  //
  //          It is important that this state survives until the next frame/loop
  //          iteration due to 1) above. Using a least two images is thus
  //          necessary. The code below uses one per swapchain, to support
  //          triple-buffering.
  //
  //       4) Call spn_render(), using a custom callback. As before, it should
  //          ensure that the VkSubmitInfo waits for the image-acquired
  //          semaphore, signals the image-rendered semaphore, but will also
  //          call presentSwapchainImage() directly after the vkQueueSubmit().
  //
  // This scheme shows an improvement of about 5% in frames/seconds with the
  // 'spinel_svg_demo' program (using --fps --no-vsync --no-clear), running on
  // the host with the lion.svg input file.
  //
  // This is the one implemented below:
  //

  // 1) Submit and present previous frame, by unsealing its composition and styling.
  LOG("FLUSHING FRAME %d\n", demo_images_.current_index());
  demo_images_.getPreviousImage().flush();

  // 2) Acquire swapchain image.
  if (!window_.acquireSwapchainImage())
    return false;

  LOG("FRAME ACQUIRED\n");

  // 3) Setup new image's composition and styling.
  uint32_t    frame_index;
  DemoImage & demo_image = demo_images_.getNextImage(&frame_index);

  demo_image.setup(frame_counter);

  // 4) Call spn_render() with the appropriate submit extensions, including
  //    a callback that will call presentSwapchainImage() just after the
  //    command buffer submission.
  SpinelVkSubmitState * spinel_submit = &spinel_submits_[frame_index];

  uint32_t image_index = window_.image_index();

  spinel_vk_submit_state_reset(spinel_submit,
                               vk_swapchain_get_image(swapchain_, image_index),
                               vk_swapchain_get_image_view(swapchain_, image_index),
                               surface_sampler_,
                               vk_swapchain_get_image_acquired_semaphore(swapchain_),
                               vk_swapchain_get_image_rendered_semaphore(swapchain_));

  if (!config_no_clear_)
    {
      const VkClearColorValue color = {
        .float32 = { 1.0f, 1.0f, 1.0f, 1.0f },
      };
      spinel_vk_submit_state_add_clear(spinel_submit, color);
    }

  spinel_vk_submit_state_add_pre_layout_transition(spinel_submit, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  spinel_vk_submit_state_add_post_layout_transition(spinel_submit, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  // This ensures that the presentSwapchainImage() call is performed as soon as Spinel
  // has submitted its command buffer(s) to the queue. See technical note above.
  auto present_callback = [](void * opaque) {
    reinterpret_cast<DemoAppSpinel *>(opaque)->window_.presentSwapchainImage();
    LOG("FRAME PRESENTED\n");
  };
  spinel_vk_submit_state_set_post_callback(spinel_submit, present_callback, this);

  LOG("FRAME RENDER\n");
  demo_image.render(spinel_vk_submit_state_get_ext(spinel_submit),
                    window_extent().width,
                    window_extent().height);

  LOG("FRAME COMPLETED\n");

  return true;
}

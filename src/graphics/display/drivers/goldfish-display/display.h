// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_DISPLAY_H_

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/control/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/device.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/types.h>

#include <list>
#include <map>
#include <memory>
#include <unordered_map>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"
#include "src/graphics/display/drivers/goldfish-display/render_control.h"
#include "src/graphics/display/drivers/goldfish-display/third_party/aosp/hwcomposer.h"

namespace goldfish {

class Display;
using DisplayType = ddk::Device<Display, ddk::ChildPreReleaseable>;

class GoldfishDisplayTest;

class Display : public DisplayType,
                public ddk::DisplayControllerImplProtocol<Display, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit Display(zx_device_t* parent);

  ~Display();

  zx_status_t Bind();

  // Device protocol implementation.
  void DdkRelease();
  void DdkChildPreRelease(void* child_ctx) {
    fbl::AutoLock lock(&flush_lock_);
    dc_intf_ = ddk::DisplayControllerInterfaceProtocolClient();
  }

  // Display controller protocol implementation.
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* interface);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_results,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count,
                                               const config_stamp_t* config_stamp);
  void DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                   size_t raw_eld_count) {}  // No ELD required for non-HDA systems.
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride);
  zx_status_t DisplayControllerImplSetDisplayPower(uint64_t display_id, bool power_on) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // TESTING ONLY
  void CreateDevices(int num_devices) {
    constexpr uint32_t dummy_width = 1024;
    constexpr uint32_t dummy_height = 768;
    constexpr uint32_t dummy_fr = 60;
    ZX_DEBUG_ASSERT(devices_.empty());
    for (int i = 0; i < num_devices; i++) {
      auto& device = devices_[i + 1];
      device.width = dummy_width;
      device.height = dummy_height;
      device.refresh_rate_hz = dummy_fr;
    }
  }
  void RemoveDevices() {
    ZX_DEBUG_ASSERT(!devices_.empty());
    devices_.clear();
    ZX_DEBUG_ASSERT(devices_.empty());
  }

  friend class GoldfishDisplayTest;

 private:
  struct ColorBuffer {
    ~ColorBuffer() = default;

    uint32_t id = 0;
    size_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    zx::vmo vmo;
    fzl::PinnedVmo pinned_vmo;
  };

  struct DisplayConfig {
    // For displays with image framebuffers attached to the display, the
    // framebuffers are represented as |ColorBuffer| in goldfish graphics
    // device implementation.
    //
    // If |layers| list is not empty, all the |layers| will be composed and
    // presented at next Vsync. Otherwise, it means the display has nothing
    // to display.
    std::list<layer_t> layers = {};

    // The |config_stamp| value of the ApplyConfiguration() call to which this
    // DisplayConfig corresponds.
    config_stamp_t config_stamp = {.value = INVALID_CONFIG_STAMP_VALUE};
  };

  // A Swapchain is a collection of ColorBuffers used as framebuffers.
  // Each time the driver composes a new frame and present it on display, it
  // requests a buffer from the Swapchain as the display buffer, and returns it
  // once the frame has completely shown on the display.
  class Swapchain {
   public:
    Swapchain() = default;
    void Add(RenderControl* rc, uint32_t width, uint32_t height, uint32_t format);
    void Add(std::unique_ptr<ColorBuffer> color_buffer);

    // Requests (borrow) an image for presentation.
    // Returns |nullptr| if there is no ColorBuffer available.
    ColorBuffer* Request();

    // This "returns" the given |cb| back to the Swapchain so that caller could
    // request it from Swapchain again.
    void Return(ColorBuffer* cb);

   private:
    std::list<std::unique_ptr<ColorBuffer>> available_buffers_;
    std::unordered_map<ColorBuffer*, std::unique_ptr<ColorBuffer>> used_buffers_;
  };

  struct Device {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t refresh_rate_hz = 60;
    uint32_t host_display_id = 0;
    float scale = 1.0;
    zx::time expected_next_flush = zx::time::infinite_past();
    config_stamp_t latest_config_stamp = {.value = INVALID_CONFIG_STAMP_VALUE};

    // Display swapchain storing framebuffers as composition targets.
    Swapchain swapchain;

    // The next display config to be posted through renderControl protocol.
    std::optional<DisplayConfig> incoming_config = std::nullopt;

    // Queues the async wait of goldfish sync device for each frame that is
    // posted (rendered) but hasn't finished rendering.
    //
    // Every time there's a new frame posted through renderControl protocol,
    // a WaitOnce waiting on the sync event for the latest config will be
    // appended to the queue. When a frame has finished rendering on host, all
    // the pending Waits that are queued no later than the frame's async Wait
    // (including the frame's Wait itself) will be popped out from the queue
    // and destroyed.
    std::list<async::WaitOnce> pending_config_waits = {};
  };

  zx_status_t ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);

  // A helper method returning the ColorBuffer layer |l| uses for primary and
  // cursor layers. It returns |nullptr| for all color layers.
  static ColorBuffer* GetLayerColorBuffer(const layer_t& l);

  // Input |layers| are created without presentation support. The driver must
  // set up all color buffers tied to the display and move all Vulkan layer
  // images out of "Vulkan-only mode" so that they can be used for presentation.
  zx_status_t ResolveInputLayers(const std::list<layer_t>& layers);

  // Create a ComposeDevice command to compose all the |layers| to ColorBuffer
  // |target| and present |target| on display |device|.
  static hwc::ComposeDeviceV2 CreateComposeDevice(const Device& device,
                                                  const std::list<layer_t>& layers,
                                                  const ColorBuffer& target);

  zx_status_t PresentDisplayConfig(RenderControl::DisplayId display_id,
                                   const DisplayConfig& display_config);
  zx_status_t SetupDisplay(uint64_t id);

  void TeardownDisplay(uint64_t id);
  void FlushDisplay(async_dispatcher_t* dispatcher, uint64_t id);

  fbl::Mutex lock_;
  ddk::GoldfishControlProtocolClient control_ TA_GUARDED(lock_);
  ddk::GoldfishPipeProtocolClient pipe_ TA_GUARDED(lock_);
  std::unique_ptr<RenderControl> rc_;

  std::map<uint64_t, Device> devices_;
  fbl::Mutex flush_lock_;
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(flush_lock_);

  async::Loop loop_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Display);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_DISPLAY_H_

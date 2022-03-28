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

#include <map>
#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

namespace goldfish {

class Display;
using DisplayType = ddk::Device<Display, ddk::ChildPreReleaseable>;

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
      Device device;
      device.width = dummy_width;
      device.height = dummy_height;
      device.refresh_rate_hz = dummy_fr;
      devices_[i + 1] = device;
    }
  }
  void RemoveDevices() {
    ZX_DEBUG_ASSERT(!devices_.empty());
    devices_.clear();
    ZX_DEBUG_ASSERT(devices_.empty());
  }

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

    zx::eventpair sync_event;
    std::unique_ptr<async::WaitOnce> async_wait;
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
  };

  struct DisplayConfig {
    // For displays with image framebuffer attached to the display, the
    // framebuffer is represented as a |ColorBuffer| in goldfish graphics
    // device implementation.
    // A configuration with a non-null |color_buffer| field means that it will
    // present this |ColorBuffer| image at Vsync; the |ColorBuffer| instance
    // will be created when importing the image and destroyed when releasing
    // the image or removing the display device. Otherwise, it means the display
    // has no framebuffers to display.
    ColorBuffer* color_buffer = nullptr;

    // The |config_stamp| value of the ApplyConfiguration() call to which this
    // DisplayConfig corresponds.
    config_stamp_t config_stamp = {.value = INVALID_CONFIG_STAMP_VALUE};
  };

  int32_t GetFbParam(uint32_t param, int32_t default_value);
  using ColorBufferId = uint32_t;
  zx::status<ColorBufferId> CreateColorBuffer(uint32_t width, uint32_t height, uint32_t format);
  zx_status_t OpenColorBuffer(ColorBufferId id);
  zx_status_t CloseColorBuffer(ColorBufferId id);

  // Zero means success; non-zero value means the call failed.
  using RcResult = int32_t;
  zx::status<RcResult> SetColorBufferVulkanMode(ColorBufferId id, uint32_t mode);
  zx::status<RcResult> UpdateColorBuffer(ColorBufferId id, const fzl::PinnedVmo& pinned_vmo,
                                         uint32_t width, uint32_t height, uint32_t format,
                                         size_t size);
  zx_status_t FbPost(uint32_t id);
  using DisplayId = uint32_t;
  zx::status<DisplayId> CreateDisplay();
  zx::status<RcResult> DestroyDisplay(DisplayId display_id);
  zx::status<RcResult> SetDisplayColorBuffer(DisplayId display_id, uint32_t id);
  zx::status<RcResult> SetDisplayPose(DisplayId display_id, int32_t x, int32_t y, uint32_t w,
                                      uint32_t h);

  zx_status_t ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
  zx_status_t PresentColorBuffer(DisplayId display_id, const DisplayConfig& display_config);
  zx_status_t SetupDisplay(uint64_t id);

  void TeardownDisplay(uint64_t id);
  void FlushDisplay(async_dispatcher_t* dispatcher, uint64_t id);

  fbl::Mutex lock_;
  ddk::GoldfishControlProtocolClient control_ TA_GUARDED(lock_);
  ddk::GoldfishPipeProtocolClient pipe_ TA_GUARDED(lock_);
  std::unique_ptr<PipeIo> pipe_io_;

  std::map<uint64_t, Device> devices_;
  fbl::Mutex flush_lock_;
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(flush_lock_);

  std::map<uint64_t, DisplayConfig> pending_config_;

  async::Loop loop_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Display);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_DISPLAY_H_

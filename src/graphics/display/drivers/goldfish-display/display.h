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
#include <lib/ddk/io-buffer.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/pmt.h>
#include <threads.h>
#include <zircon/types.h>

#include <map>
#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

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
  zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_results,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count);
  void DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                   size_t raw_eld_count) {}  // No ELD required for non-HDA systems.
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride);

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
    ~ColorBuffer() {
      if (pmt)
        pmt.unpin();
    }

    uint32_t id = 0;
    zx_paddr_t paddr = 0;
    size_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    zx::vmo vmo;
    zx::pmt pmt;

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
  };

  zx_status_t WriteLocked(uint32_t cmd_size) TA_REQ(lock_);
  zx_status_t ReadResultLocked(uint32_t* result, uint32_t count) TA_REQ(lock_);
  zx_status_t ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result) TA_REQ(lock_);
  int32_t GetFbParamLocked(uint32_t param, int32_t default_value) TA_REQ(lock_);
  zx_status_t CreateColorBufferLocked(uint32_t width, uint32_t height, uint32_t format,
                                      uint32_t* id) TA_REQ(lock_);
  zx_status_t OpenColorBufferLocked(uint32_t id) TA_REQ(lock_);
  zx_status_t CloseColorBufferLocked(uint32_t id) TA_REQ(lock_);
  zx_status_t SetColorBufferVulkanModeLocked(uint32_t id, uint32_t mode, uint32_t* result)
      TA_REQ(lock_);
  zx_status_t UpdateColorBufferLocked(uint32_t id, zx_paddr_t paddr, uint32_t width,
                                      uint32_t height, uint32_t format, size_t size,
                                      uint32_t* result) TA_REQ(lock_);
  zx_status_t FbPostLocked(uint32_t id) TA_REQ(lock_);
  zx_status_t CreateDisplayLocked(uint32_t* result) TA_REQ(lock_);
  zx_status_t DestroyDisplayLocked(uint32_t display_id, uint32_t* result) TA_REQ(lock_);
  zx_status_t SetDisplayColorBufferLocked(uint32_t display_id, uint32_t id, uint32_t* result)
      TA_REQ(lock_);
  zx_status_t SetDisplayPoseLocked(uint32_t display_id, int32_t x, int32_t y, uint32_t w,
                                   uint32_t h, uint32_t* result) TA_REQ(lock_);
  zx_status_t ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);

  zx_status_t PresentColorBuffer(uint32_t display_id, ColorBuffer* color_buffer);
  zx_status_t SetupDisplayLocked(uint64_t id) TA_REQ(lock_);
  void TeardownDisplay(uint64_t id);
  void FlushDisplay(async_dispatcher_t* dispatcher, uint64_t id);

  fbl::Mutex lock_;
  ddk::GoldfishControlProtocolClient control_ TA_GUARDED(lock_);
  ddk::GoldfishPipeProtocolClient pipe_ TA_GUARDED(lock_);
  int32_t id_ = 0;
  zx::bti bti_;
  ddk::IoBuffer cmd_buffer_ TA_GUARDED(lock_);
  ddk::IoBuffer io_buffer_ TA_GUARDED(lock_);

  std::map<uint64_t, Device> devices_;
  fbl::Mutex flush_lock_;
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(flush_lock_);
  std::map<uint64_t, ColorBuffer*> current_cb_;
  std::map<uint64_t, ColorBuffer*> pending_cb_;

  zx::event pipe_event_;

  async::Loop loop_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Display);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_DISPLAY_H_

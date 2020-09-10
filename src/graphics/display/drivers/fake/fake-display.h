// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/display/capture.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/capture.h>
#include <ddktl/protocol/display/clamprgb.h>
#include <ddktl/protocol/display/controller.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "ddk/protocol/display/clamprgb.h"

namespace fake_display {

struct ImageInfo : public fbl::DoublyLinkedListable<std::unique_ptr<ImageInfo>> {
  zx::vmo vmo;
};

class FakeDisplay;
class ClampRgb;

using DeviceType = ddk::Device<FakeDisplay, ddk::GetProtocolable, ddk::Unbindable>;

class FakeDisplay : public DeviceType,
                    public ddk::DisplayControllerImplProtocol<FakeDisplay, ddk::base_protocol>,
                    public ddk::DisplayCaptureImplProtocol<FakeDisplay>,
                    public ddk::DisplayClampRgbImplProtocol<FakeDisplay> {
 public:
  explicit FakeDisplay(zx_device_t* parent)
      : DeviceType(parent),
        dcimpl_proto_({&display_controller_impl_protocol_ops_, this}),
        clamp_rgbimpl_proto_({&display_clamp_rgb_impl_protocol_ops_, this}) {}

  // This function is called from the c-bind function upon driver matching. If start_vsync is
  // true, a background thread will be started to issue vsync events.
  zx_status_t Bind(bool start_vsync);

  // Required functions needed to implement Display Controller Protocol
  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);
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
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void DisplayCaptureImplSetDisplayCaptureInterface(
      const display_capture_interface_protocol_t* intf);

  zx_status_t DisplayCaptureImplImportImageForCapture(zx_unowned_handle_t collection,
                                                      uint32_t index, uint64_t* out_capture_handle)
      __TA_EXCLUDES(capture_lock_);
  zx_status_t DisplayCaptureImplStartCapture(uint64_t capture_handle) __TA_EXCLUDES(capture_lock_);
  zx_status_t DisplayCaptureImplReleaseCapture(uint64_t capture_handle)
      __TA_EXCLUDES(capture_lock_);
  bool DisplayCaptureImplIsCaptureCompleted() __TA_EXCLUDES(capture_lock_);
  zx_status_t DisplayClampRgbImplSetMinimumRgb(uint8_t minimum_rgb);

  // Required functions for DeviceType
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);

  const display_controller_impl_protocol_t* dcimpl_proto() const { return &dcimpl_proto_; }
  const display_clamp_rgb_impl_protocol_t* clamp_rgbimpl_proto() const {
    return &clamp_rgbimpl_proto_;
  }
  void SendVsync();

  // Just for display core unittests.
  zx_status_t ImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);

  size_t TEST_imported_images_count() const {
    fbl::AutoLock lock(&image_lock_);
    return imported_images_.size_slow();
  }

  uint8_t GetClampRgbValue() const { return clamp_rgb_value_; }

 private:
  enum {
    FRAGMENT_PDEV,
    FRAGMENT_SYSMEM,
    FRAGMENT_COUNT,
  };
  zx_device_t* fragments_[FRAGMENT_COUNT];

  zx_status_t SetupDisplayInterface();
  int VSyncThread();
  int CaptureThread() __TA_EXCLUDES(capture_lock_, display_lock_);
  void PopulateAddedDisplayArgs(added_display_args_t* args);

  display_controller_impl_protocol_t dcimpl_proto_ = {};
  display_clamp_rgb_impl_protocol_t clamp_rgbimpl_proto_ = {};
  pdev_protocol_t pdev_ = {};
  sysmem_protocol_t sysmem_ = {};

  std::atomic_bool vsync_shutdown_flag_ = false;
  std::atomic_bool capture_shutdown_flag_ = false;

  // Thread handles
  bool vsync_thread_running_ = false;
  thrd_t vsync_thread_;
  thrd_t capture_thread_;

  // Locks used by the display driver
  fbl::Mutex display_lock_;        // general display state (i.e. display_id)
  mutable fbl::Mutex image_lock_;  // used for accessing imported_images
  fbl::Mutex capture_lock_;        // general capture state

  // The ID for currently active capture
  uint64_t capture_active_id_ TA_GUARDED(capture_lock_);

  // Imported Images
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_images_ TA_GUARDED(image_lock_);
  fbl::DoublyLinkedList<std::unique_ptr<ImageInfo>> imported_captures_ TA_GUARDED(capture_lock_);

  uint64_t current_image_ TA_GUARDED(display_lock_);
  bool current_image_valid_ TA_GUARDED(display_lock_);

  // Capture complete is signaled at vsync time. This counter introduces a bit of delay
  // for signal capture complete
  uint64_t capture_complete_signal_count_ = 0;

  // Value that holds the clamped RGB value
  uint8_t clamp_rgb_value_ = 0;

  // Display controller related data
  ddk::DisplayControllerInterfaceProtocolClient dc_intf_ TA_GUARDED(display_lock_);

  // Display Capture interface protocol
  ddk::DisplayCaptureInterfaceProtocolClient capture_intf_ TA_GUARDED(capture_lock_);
};

}  // namespace fake_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CONTROLLER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CONTROLLER_H_

// NOTE: this header is included first on purpose to avoid clashing with
// Banjo-generated ones below.
// clang-format off
#include <fuchsia/hardware/display/llcpp/fidl.h>

#include <fuchsia/hardware/audiotypes/c/banjo.h>
#include <fuchsia/hardware/display/capture/cpp/banjo.h>
#include <fuchsia/hardware/display/clamprgb/cpp/banjo.h>
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
// clang-format on
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/edid/edid.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include <functional>
#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/vector.h>

#include "id-map.h"
#include "image.h"
#include "src/lib/async-watchdog/watchdog.h"

namespace display {

class ClientProxy;
class Controller;
class ControllerTest;
class DisplayConfig;
class IntegrationTest;

class DisplayInfo : public IdMappable<fbl::RefPtr<DisplayInfo>>,
                    public fbl::RefCounted<DisplayInfo> {
 private:
  DisplayInfo() = default;
  void PopulateDisplayAudio();

 public:
  static zx::status<fbl::RefPtr<DisplayInfo>> Create(
      const added_display_args_t& info, ddk::I2cImplProtocolClient* i2c);

  // Should be called after init_done is set to true.
  void InitializeInspect(inspect::Node* parent_node);

  bool has_edid;
  edid::Edid edid;
  fbl::Vector<edid::timing_params_t> edid_timings;
  fbl::Vector<audio_types_audio_stream_format_range_t> edid_audio_;

  // This field has no meaning if EDID information is available.
  display_params_t params;

  fbl::Array<zx_pixel_format_t> pixel_formats_;
  fbl::Array<cursor_info_t> cursor_infos_;

  // Flag indicating that the display is ready to be published to clients.
  bool init_done = false;

  // A list of all images which have been sent to display driver. For multiple
  // images which are displayed at the same time, images with a lower z-order
  // occur first.
  list_node_t images = LIST_INITIAL_VALUE(images);
  // The number of layers in the applied configuration which are important for vsync (i.e.
  // that have images).
  uint32_t vsync_layer_count;

  // Set when a layer change occurs on this display and cleared in vsync
  // when the new layers are all active.
  bool pending_layer_change;
  // Flag indicating that a new configuration was delayed during a layer change
  // and should be reapplied after the layer change completes.
  bool delayed_apply;

  // True when we're in the process of switching between display clients.
  bool switching_client = false;

  inspect::Node node;
  inspect::ValueList properties;
};

using ControllerParent = ddk::Device<Controller, ddk::Unbindable, ddk::Openable,
                                     ddk::Messageable<fuchsia_hardware_display::Provider>::Mixin>;
class Controller : public ControllerParent,
                   public ddk::DisplayControllerInterfaceProtocol<Controller>,
                   public ddk::DisplayCaptureInterfaceProtocol<Controller>,
                   public ddk::EmptyProtocol<ZX_PROTOCOL_DISPLAY_CONTROLLER> {
 public:
  Controller(zx_device_t* parent);
  ~Controller();

  static void PopulateDisplayMode(const edid::timing_params_t& params, display_mode_t* mode);

  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t Bind(std::unique_ptr<display::Controller>* device_ptr);

  void DisplayControllerInterfaceOnDisplaysChanged(const added_display_args_t* displays_added,
                                                   size_t added_count,
                                                   const uint64_t* displays_removed,
                                                   size_t removed_count,
                                                   added_display_info_t* out_display_info_list,
                                                   size_t display_info_count,
                                                   size_t* display_info_actual);
  void DisplayControllerInterfaceOnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                                const uint64_t* handles, size_t handle_count);
  zx_status_t DisplayControllerInterfaceGetAudioFormat(
      uint64_t display_id, uint32_t fmt_idx, audio_types_audio_stream_format_range_t* fmt_out);

  void DisplayCaptureInterfaceOnCaptureComplete();
  void OnClientDead(ClientProxy* client);
  void SetVcMode(uint8_t mode);
  void ShowActiveDisplay();

  void ApplyConfig(DisplayConfig* configs[], int32_t count, bool vc_client, uint32_t apply_stamp,
                   uint32_t client_id) __TA_EXCLUDES(mtx());

  void ReleaseImage(Image* image);
  void ReleaseCaptureImage(uint64_t handle);

  // |mtx()| must be held for as long as |edid| and |params| are retained.
  bool GetPanelConfig(uint64_t display_id, const fbl::Vector<edid::timing_params_t>** timings,
                      const display_params_t** params) __TA_REQUIRES(mtx());
  bool GetSupportedPixelFormats(uint64_t display_id, fbl::Array<zx_pixel_format_t>* fmts_out)
      __TA_REQUIRES(mtx());
  bool GetCursorInfo(uint64_t display_id, fbl::Array<cursor_info_t>* cursor_info_out)
      __TA_REQUIRES(mtx());
  bool GetDisplayIdentifiers(uint64_t display_id, const char** manufacturer_name,
                             const char** monitor_name, const char** monitor_serial)
      __TA_REQUIRES(mtx());
  bool GetDisplayPhysicalDimensions(uint64_t display_id, uint32_t* horizontal_size_mm,
                                    uint32_t* vertical_size_mm) __TA_REQUIRES(mtx());
  ddk::DisplayControllerImplProtocolClient* dc() { return &dc_; }
  ddk::DisplayCaptureImplProtocolClient* dc_capture() {
    if (dc_capture_.is_valid()) {
      return &dc_capture_;
    }
    return nullptr;
  }
  ddk::DisplayClampRgbImplProtocolClient* dc_clamp_rgb() {
    if (dc_clamp_rgb_.is_valid()) {
      return &dc_clamp_rgb_;
    }
    return nullptr;
  }
  async::Loop& loop() { return loop_; }
  bool current_thread_is_loop() { return thrd_current() == loop_thread_; }
  // Thread-safety annotations currently don't deal with pointer aliases. Use this to document
  // places where we believe a mutex aliases mtx()
  void AssertMtxAliasHeld(mtx_t* m) __TA_ASSERT(m) { ZX_DEBUG_ASSERT(m == mtx()); }
  mtx_t* mtx() const { return &mtx_; }

  // Test helpers
  size_t TEST_imported_images_count() const;

  // Typically called by OpenController/OpenVirtconController.  However, this is made public
  // for use by testing services which provide a fake display controller.
  zx_status_t CreateClient(bool is_vc, zx::channel device, zx::channel client,
                           fit::function<void()> on_client_dead = nullptr);

 private:
  friend ControllerTest;
  friend IntegrationTest;

  void HandleClientOwnershipChanges() __TA_REQUIRES(mtx());
  void PopulateDisplayTimings(const fbl::RefPtr<DisplayInfo>& info) __TA_EXCLUDES(mtx());

  void OpenVirtconController(OpenVirtconControllerRequestView request,
                             OpenVirtconControllerCompleter::Sync& _completer) override;
  void OpenController(OpenControllerRequestView request,
                      OpenControllerCompleter::Sync& _completer) override;

  // Periodically reads |last_vsync_timestamp_| and increments |vsync_stalls_detected_| if no vsync
  // has been observed in a given time period.
  void OnVsyncMonitor();

  inspect::Inspector inspector_;
  // Currently located at bootstrap/driver_manager:root/display.
  inspect::Node root_;

  bool kernel_framebuffer_enabled_;

  // mtx_ is a global lock on state shared among clients.
  mutable mtx_t mtx_;
  bool unbinding_ __TA_GUARDED(mtx()) = false;

  DisplayInfo::Map displays_ __TA_GUARDED(mtx());
  bool vc_applied_ = false;
  uint32_t applied_stamp_ = UINT32_MAX;
  uint32_t applied_client_id_ = 0;
  uint64_t pending_capture_image_release_ = 0;

  uint32_t next_client_id_ __TA_GUARDED(mtx()) = 1;
  ClientProxy* vc_client_ __TA_GUARDED(mtx()) = nullptr;
  bool vc_ready_ __TA_GUARDED(mtx());
  ClientProxy* primary_client_ __TA_GUARDED(mtx()) = nullptr;
  bool primary_ready_ __TA_GUARDED(mtx());
  fuchsia_hardware_display::wire::VirtconMode vc_mode_ __TA_GUARDED(mtx()) =
      fuchsia_hardware_display::wire::VirtconMode::kInactive;
  ClientProxy* active_client_ __TA_GUARDED(mtx()) = nullptr;

  async::Loop loop_;
  thrd_t loop_thread_;
  async_watchdog::Watchdog watchdog_;
  ddk::DisplayControllerImplProtocolClient dc_;
  ddk::DisplayCaptureImplProtocolClient dc_capture_;
  ddk::DisplayClampRgbImplProtocolClient dc_clamp_rgb_;
  ddk::I2cImplProtocolClient i2c_;

  std::atomic<zx::time> last_vsync_timestamp_{};
  inspect::UintProperty last_vsync_ns_property_;
  inspect::UintProperty last_vsync_interval_ns_property_;

  // Fields that track how often vsync was detected to have been stalled.
  std::atomic_bool vsync_stalled_ = false;
  inspect::UintProperty vsync_stalls_detected_;
  async::TaskClosureMethod<Controller, &Controller::OnVsyncMonitor> vsync_monitor_{this};

  zx_time_t last_valid_apply_config_timestamp_{};
  inspect::UintProperty last_valid_apply_config_timestamp_ns_property_;
  inspect::UintProperty last_valid_apply_config_interval_ns_property_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CONTROLLER_H_

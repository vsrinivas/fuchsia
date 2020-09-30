// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CLIENT_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CLIENT_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/vector.h>

#include "controller.h"
#include "fbl/ref_ptr.h"
#include "fbl/ring_buffer.h"
#include "fence.h"
#include "id-map.h"
#include "image.h"
#include "layer.h"
#include "lib/fidl-async/cpp/bind.h"
#include "lib/fidl/llcpp/array.h"
#include "lib/fidl/llcpp/server.h"

namespace display {

class GammaTables : public fbl::RefCounted<GammaTables> {
 public:
  static constexpr uint32_t kTableSize = 256;
  explicit GammaTables(::fidl::Array<float, kTableSize> r, ::fidl::Array<float, kTableSize> g,
                       ::fidl::Array<float, kTableSize> b) {
    std::memcpy(&red, &r, sizeof(r.data_));
    std::memcpy(&green, &g, sizeof(g.data_));
    std::memcpy(&blue, &b, sizeof(b.data_));
  }

  // We are returning raw pointers here for display driver consumption. However,
  // a ref-counted pointer is held by core display to guarantee validity of the
  // pointers.
  float* Red() { return red.data(); }
  float* Green() { return green.data(); }
  float* Blue() { return blue.data(); }

 private:
  ::fidl::Array<float, kTableSize> red;
  ::fidl::Array<float, kTableSize> green;
  ::fidl::Array<float, kTableSize> blue;
};

// Almost-POD used by Client to manage display configuration. Public state is used by Controller.
class DisplayConfig : public IdMappable<std::unique_ptr<DisplayConfig>> {
 public:
  bool apply_layer_change() {
    bool ret = pending_apply_layer_change_;
    pending_apply_layer_change_ = false;
    return ret;
  }

  uint32_t vsync_layer_count() const { return vsync_layer_count_; }
  const display_config_t* current_config() const { return &current_; }
  const fbl::SinglyLinkedList<layer_node_t*>& get_current_layers() const { return current_layers_; }

 private:
  display_config_t current_;
  display_config_t pending_;

  fbl::RefPtr<GammaTables> pending_gamma_table_;
  fbl::RefPtr<GammaTables> current_gamma_table_;

  bool pending_layer_change_;
  bool pending_apply_layer_change_;
  fbl::SinglyLinkedList<layer_node_t*> pending_layers_;
  fbl::SinglyLinkedList<layer_node_t*> current_layers_;

  fbl::Array<zx_pixel_format_t> pixel_formats_;
  fbl::Array<cursor_info_t> cursor_infos_;

  uint32_t vsync_layer_count_ = 0xffffffff;
  bool display_config_change_ = false;

  friend Client;
  friend ClientProxy;
};

// The Client class manages all state associated with an open display client
// connection. Other than initialization, all methods of this class execute on
// on the controller's looper, so no synchronization is necessary.
class Client : public llcpp::fuchsia::hardware::display::Controller::Interface {
 public:
  // |controller| must outlive this and |proxy|.
  Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t id);

  // This is used for testing
  Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t id,
         zx::channel server_channel);

  ~Client();
  zx_status_t Init(zx::channel server_channel);

  void OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                         const uint64_t* displays_removed, size_t removed_count);
  void SetOwnership(bool is_owner);
  void ApplyConfig();

  void OnFenceFired(FenceReference* fence);

  void TearDown();
  // This is used for testing
  void TearDownTest();

  bool IsValid() { return server_handle_ != ZX_HANDLE_INVALID; }
  uint32_t id() const { return id_; }
  void CaptureCompleted();

  uint8_t GetMinimumRgb() const { return client_minimum_rgb_; }

  // Test helpers
  size_t TEST_imported_images_count() const { return images_.size(); }

  void CancelFidlBind() {
    if (fidl_binding_.has_value()) {
      fidl_binding_->Unbind();
      fidl_binding_.reset();
    }
  }

  // Used for testing
  sync_completion_t* fidl_unbound() { return &fidl_unbound_; }
  uint64_t LatestAckedCookie() const { return acked_cookie_; }
  size_t GetGammaTableSize() const { return gamma_table_map_.size(); }

 private:
  void ImportVmoImage(llcpp::fuchsia::hardware::display::ImageConfig image_config, zx::vmo vmo,
                      int32_t offset, ImportVmoImageCompleter::Sync& _completer) override;
  void ImportImage(llcpp::fuchsia::hardware::display::ImageConfig image_config,
                   uint64_t collection_id, uint32_t index,
                   ImportImageCompleter::Sync& _completer) override;
  void ReleaseImage(uint64_t image_id, ReleaseImageCompleter::Sync& _completer) override;
  void ImportEvent(zx::event event, uint64_t id, ImportEventCompleter::Sync& _completer) override;
  void ReleaseEvent(uint64_t id, ReleaseEventCompleter::Sync& _completer) override;
  void CreateLayer(CreateLayerCompleter::Sync& _completer) override;
  void DestroyLayer(uint64_t layer_id, DestroyLayerCompleter::Sync& _completer) override;
  void ImportGammaTable(uint64_t gamma_table_id, ::fidl::Array<float, 256> r,
                        ::fidl::Array<float, 256> g, ::fidl::Array<float, 256> b,
                        ImportGammaTableCompleter::Sync& _completer) override;
  void ReleaseGammaTable(uint64_t gamma_table_id,
                         ReleaseGammaTableCompleter::Sync& _completer) override;
  void SetDisplayMode(uint64_t display_id, llcpp::fuchsia::hardware::display::Mode mode,
                      SetDisplayModeCompleter::Sync& _completer) override;
  void SetDisplayColorConversion(uint64_t display_id, ::fidl::Array<float, 3> preoffsets,
                                 ::fidl::Array<float, 9> coefficients,
                                 ::fidl::Array<float, 3> postoffsets,
                                 SetDisplayColorConversionCompleter::Sync& _completer) override;
  void SetDisplayGammaTable(uint64_t display_id, uint64_t gamma_table_id,
                            SetDisplayGammaTableCompleter::Sync& _completer) override;
  void SetDisplayLayers(uint64_t display_id, ::fidl::VectorView<uint64_t> layer_ids,
                        SetDisplayLayersCompleter::Sync& _completer) override;
  void SetLayerPrimaryConfig(uint64_t layer_id,
                             llcpp::fuchsia::hardware::display::ImageConfig image_config,
                             SetLayerPrimaryConfigCompleter::Sync& _completer) override;
  void SetLayerPrimaryPosition(uint64_t layer_id,
                               llcpp::fuchsia::hardware::display::Transform transform,
                               llcpp::fuchsia::hardware::display::Frame src_frame,
                               llcpp::fuchsia::hardware::display::Frame dest_frame,
                               SetLayerPrimaryPositionCompleter::Sync& _completer) override;
  void SetLayerPrimaryAlpha(uint64_t layer_id, llcpp::fuchsia::hardware::display::AlphaMode mode,
                            float val, SetLayerPrimaryAlphaCompleter::Sync& _completer) override;
  void SetLayerCursorConfig(uint64_t layer_id,
                            llcpp::fuchsia::hardware::display::ImageConfig image_config,
                            SetLayerCursorConfigCompleter::Sync& _completer) override;
  void SetLayerCursorPosition(uint64_t layer_id, int32_t x, int32_t y,
                              SetLayerCursorPositionCompleter::Sync& _completer) override;
  void SetLayerColorConfig(uint64_t layer_id, uint32_t pixel_format,
                           ::fidl::VectorView<uint8_t> color_bytes,
                           SetLayerColorConfigCompleter::Sync& _completer) override;
  void SetLayerImage(uint64_t layer_id, uint64_t image_id, uint64_t wait_event_id,
                     uint64_t signal_event_id, SetLayerImageCompleter::Sync& _completer) override;
  void CheckConfig(bool discard, CheckConfigCompleter::Sync& _completer) override;
  void ApplyConfig(ApplyConfigCompleter::Sync& _completer) override;
  void EnableVsync(bool enable, EnableVsyncCompleter::Sync& _completer) override;
  void SetVirtconMode(uint8_t mode, SetVirtconModeCompleter::Sync& _completer) override;
  void GetSingleBufferFramebuffer(GetSingleBufferFramebufferCompleter::Sync& _completer) override;
  void ImportBufferCollection(uint64_t collection_id, zx::channel collection_token,
                              ImportBufferCollectionCompleter::Sync& _completer) override;
  void SetBufferCollectionConstraints(
      uint64_t collection_id, llcpp::fuchsia::hardware::display::ImageConfig config,
      SetBufferCollectionConstraintsCompleter::Sync& _completer) override;
  void ReleaseBufferCollection(uint64_t collection_id,
                               ReleaseBufferCollectionCompleter::Sync& _completer) override;

  void IsCaptureSupported(IsCaptureSupportedCompleter::Sync& _completer) override;
  void ImportImageForCapture(llcpp::fuchsia::hardware::display::ImageConfig image_config,
                             uint64_t collection_id, uint32_t index,
                             ImportImageForCaptureCompleter::Sync& _completer) override;

  void StartCapture(uint64_t signal_event_id, uint64_t image_id,
                    StartCaptureCompleter::Sync& _completer) override;

  void ReleaseCapture(uint64_t image_id, ReleaseCaptureCompleter::Sync& _completer) override;

  void AcknowledgeVsync(uint64_t cookie, AcknowledgeVsyncCompleter::Sync& _completer) override;

  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCompleter::Sync& _completer) override;

  // Cleans up layer state associated with an image. If image == nullptr, then
  // cleans up all image state. Return true if a current layer was modified.
  bool CleanUpImage(Image* image);
  void CleanUpCaptureImage();

  Controller* controller_;
  ClientProxy* proxy_;
  bool is_vc_;
  uint64_t console_fb_display_id_ = -1;
  const uint32_t id_;
  uint32_t single_buffer_framebuffer_stride_ = 0;
  zx::channel server_channel_;  // used for unit-testing
  zx_handle_t server_handle_;
  uint64_t next_image_id_ = 1;         // Only INVALID_ID == 0 is invalid
  uint64_t next_capture_image_id = 1;  // Only INVALID_ID == 0 is invalid
  Image::Map images_;
  Image::Map capture_images_;
  DisplayConfig::Map configs_;
  bool pending_config_valid_ = false;
  bool is_owner_ = false;
  // A counter for the number of times the client has successfully applied
  // a configuration. This does not account for changes due to waiting images.
  uint32_t client_apply_count_ = 0;

  // This is the client's clamped RGB value.
  uint8_t client_minimum_rgb_ = 0;
  sync_completion_t fidl_unbound_;

  llcpp::fuchsia::sysmem::Allocator::SyncClient sysmem_allocator_;

  struct Collections {
    // Sent to the hardware driver.
    llcpp::fuchsia::sysmem::BufferCollection::SyncClient driver;
    // If the VC is using this, |kernel| is the collection used for setting
    // it as kernel framebuffer.
    llcpp::fuchsia::sysmem::BufferCollection::SyncClient kernel;
  };
  std::map<uint64_t, Collections> collection_map_;

  FenceCollection fences_;

  Layer::Map layers_;
  uint64_t next_layer_id = 1;

  // TODO(stevensd): Delete this when client stop using SetDisplayImage
  uint64_t display_image_layer_ = INVALID_ID;

  void NotifyDisplaysChanged(const int32_t* displays_added, uint32_t added_count,
                             const int32_t* displays_removed, uint32_t removed_count);
  bool CheckConfig(llcpp::fuchsia::hardware::display::ConfigResult* res,
                   std::vector<llcpp::fuchsia::hardware::display::ClientCompositionOp>* ops);

  uint64_t GetActiveCaptureImage() { return current_capture_image_; }

  fit::optional<fidl::ServerBindingRef<llcpp::fuchsia::hardware::display::Controller>>
      fidl_binding_;
  // This is the channel returned by fidl bind during unbound
  zx::channel fidl_channel_;
  // Capture related book keeping
  uint64_t capture_fence_id_ = INVALID_ID;
  uint64_t current_capture_image_ = INVALID_ID;
  uint64_t pending_capture_release_image_ = INVALID_ID;

  uint64_t acked_cookie_ = 0;

  std::map<uint64_t, fbl::RefPtr<GammaTables>> gamma_table_map_;
};

// ClientProxy manages interactions between its Client instance and the ddk and the
// controller. Methods on this class are thread safe.
using ClientParent = ddk::Device<ClientProxy, ddk::Unbindable, ddk::Closable>;
class ClientProxy : public ClientParent {
 public:
  // "client_id" is assigned by the Controller to distinguish clients.
  ClientProxy(Controller* controller, bool is_vc, uint32_t client_id);

  // This is used for testing
  ClientProxy(Controller* controller, bool is_vc, uint32_t client_id, zx::channel server_channel);

  ~ClientProxy();
  zx_status_t Init(zx::channel server_channel);

  zx_status_t DdkClose(uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // Requires holding controller_->mtx() lock
  zx_status_t OnDisplayVsync(uint64_t display_id, zx_time_t timestamp, uint64_t* image_ids,
                             size_t count);
  void OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                         const uint64_t* displays_removed, size_t removed_count);
  void SetOwnership(bool is_owner);
  void ReapplyConfig();
  zx_status_t OnCaptureComplete();

  void EnableVsync(bool enable) {
    fbl::AutoLock lock(&mtx_);
    enable_vsync_ = enable;
  }

  void EnableCapture(bool enable) {
    fbl::AutoLock lock(&mtx_);
    enable_capture_ = enable;
  }
  void OnClientDead();

  // This function restores client configurations that are not part of
  // the standard configuration. These configurations are typically one-time
  // settings that need to get restored once client takes control again.
  void ReapplySpecialConfigs();

  uint32_t id() const { return handler_.id(); }

  // This is used for testing
  void CloseTest();

  // Test helpers
  size_t TEST_imported_images_count() const { return handler_.TEST_imported_images_count(); }

  // Define these constants here so we can access it for test

  static constexpr uint32_t kVsyncBufferSize = 10;

  // Maximum number of vsync messages sent before an acknowledgement is required.
  // Half of this limit is provided to clients as part of display info. Assuming a
  // frame rate of 60hz, clients will be required to acknowledge at least once a second
  // and driver will stop sending messages after 2 seconds of no acknowledgement
  static constexpr uint32_t kMaxVsyncMessages = 120;
  static constexpr uint32_t kVsyncMessagesWatermark = (kMaxVsyncMessages / 2);
  // At the moment, maximum image handles returned by any driver is 4 which is
  // equal to number of hardware layers. 8 should be more than enough to allow for
  // a simple statically allocated array of image_ids for vsync events that are being
  // stored due to client non-acknowledgement.
  static constexpr uint32_t kMaxImageHandles = 8;

 protected:
  void CloseOnControllerLoop();
  friend IntegrationTest;

  mtx_t mtx_;
  Controller* controller_;
  bool is_vc_;
  // server_channel_ will be passed to handler_ which will in turn pass it to
  // fidl::BindSingleInFlightOnly who will own the channel.
  zx::unowned_channel server_channel_;
  Client handler_;
  bool enable_vsync_ __TA_GUARDED(&mtx_) = false;
  bool enable_capture_ __TA_GUARDED(&mtx_) = false;

  mtx_t task_mtx_;
  std::vector<std::unique_ptr<async::Task>> client_scheduled_tasks_ __TA_GUARDED(task_mtx_);

  // This variable is used to limit the number of errors logged in case of channel oom error
  static constexpr uint32_t kChannelOomPrintFreq = 600;  // 1 per 10 seconds (assuming 60fps)
  uint32_t chn_oom_print_freq_ = 0;
  uint64_t total_oom_errors_ = 0;

  using vsync_msg_t = struct vsync_msg {
    uint64_t display_id;
    zx_time_t timestamp;
    uint64_t image_ids[kMaxImageHandles];
    size_t count;
  };

  fbl::RingBuffer<vsync_msg_t, kVsyncBufferSize> buffered_vsync_messages_;
  uint64_t initial_cookie_ = 0;
  uint64_t cookie_sequence_ = 0;

  uint64_t number_of_vsyncs_sent_ = 0;
  uint64_t last_cookie_sent_ = 0;
  bool acknowledge_request_sent_ = false;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_CLIENT_H_

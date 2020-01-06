// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_CLIENT_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_CLIENT_H_

#include <fuchsia/hardware/display/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <map>
#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/vector.h>

#include "controller.h"
#include "fence.h"
#include "id-map.h"
#include "image.h"

namespace display {

class Layer;
class Client;

typedef struct layer_node : public fbl::SinglyLinkedListable<layer_node*> {
  Layer* layer;
} layer_node_t;

// Almost-POD used by Client to manage layer state. Public state is used by Controller.
class Layer : public IdMappable<std::unique_ptr<Layer>> {
 public:
  fbl::RefPtr<Image> current_image() const { return displayed_image_; }
  uint32_t z_order() const { return current_layer_.z_index; }
  bool is_skipped() const { return is_skipped_; }

 private:
  layer_t pending_layer_;
  layer_t current_layer_;
  // flag indicating that there are changes in pending_layer that
  // need to be applied to current_layer.
  bool config_change_;

  // Event ids passed to SetLayerImage which haven't been applied yet.
  uint64_t pending_wait_event_id_;
  uint64_t pending_signal_event_id_;

  // The image given to SetLayerImage which hasn't been applied yet.
  fbl::RefPtr<Image> pending_image_;

  // Image which are waiting to be displayed
  list_node_t waiting_images_ = LIST_INITIAL_VALUE(waiting_images_);
  // The image which has most recently been sent to the display controller impl
  fbl::RefPtr<Image> displayed_image_;

  // Counters used for keeping track of when the layer's images need to be dropped.
  uint64_t pending_image_config_gen_ = 0;
  uint64_t current_image_config_gen_ = 0;

  int32_t pending_cursor_x_;
  int32_t pending_cursor_y_;
  int32_t current_cursor_x_;
  int32_t current_cursor_y_;

  // Storage for a color layer's color data bytes.
  uint8_t pending_color_bytes_[4];
  uint8_t current_color_bytes_[4];

  layer_node_t pending_node_;
  layer_node_t current_node_;

  // The display this layer was most recently displayed on
  uint64_t current_display_id_;

  bool is_skipped_;

  friend Client;
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

  bool pending_layer_change_;
  bool pending_apply_layer_change_;
  fbl::SinglyLinkedList<layer_node_t*> pending_layers_;
  fbl::SinglyLinkedList<layer_node_t*> current_layers_;

  fbl::Array<zx_pixel_format_t> pixel_formats_;
  fbl::Array<cursor_info_t> cursor_infos_;

  uint32_t vsync_layer_count_;
  bool display_config_change_ = false;

  friend Client;
  friend ClientProxy;
};

// The Client class manages all state associated with an open display client
// connection. Other than initialization, all methods of this class execute on
// on the controller's looper, so no synchronization is necessary.
class Client : private FenceCallback {
 public:
  // |controller| must outlive this and |proxy|.
  Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t id);

  // This is used for testing
  Client(Controller* controller, ClientProxy* proxy, bool is_vc, uint32_t id,
         zx_handle_t server_handle);

  ~Client();
  zx_status_t Init(zx_handle_t server_handle);

  void OnDisplaysChanged(const uint64_t* displays_added, size_t added_count,
                         const uint64_t* displays_removed, size_t removed_count);
  void SetOwnership(bool is_owner);
  void ApplyConfig();

  void OnFenceFired(FenceReference* fence) override;
  void OnRefForFenceDead(Fence* fence) __TA_EXCLUDES(fence_mtx_) override;

  void TearDown() __TA_EXCLUDES(fence_mtx_);

  // This is used for testing
  void TearDownTest();

  bool IsValid() { return server_handle_ != ZX_HANDLE_INVALID; }
  uint32_t id() const { return id_; }
  void CaptureCompleted();

 private:
  void HandleImportVmoImage(const fuchsia_hardware_display_ControllerImportVmoImageRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleImportImage(const fuchsia_hardware_display_ControllerImportImageRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleReleaseImage(const fuchsia_hardware_display_ControllerReleaseImageRequest* req,
                          fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  bool ImportEvent(zx::event event, uint64_t id) __TA_EXCLUDES(fence_mtx_);
  void HandleImportEvent(const fuchsia_hardware_display_ControllerImportEventRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleReleaseEvent(const fuchsia_hardware_display_ControllerReleaseEventRequest* req,
                          fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleCreateLayer(const fuchsia_hardware_display_ControllerCreateLayerRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleDestroyLayer(const fuchsia_hardware_display_ControllerDestroyLayerRequest* req,
                          fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetDisplayMode(const fuchsia_hardware_display_ControllerSetDisplayModeRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetDisplayColorConversion(
      const fuchsia_hardware_display_ControllerSetDisplayColorConversionRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetDisplayLayers(const fuchsia_hardware_display_ControllerSetDisplayLayersRequest* req,
                              fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerPrimaryConfig(
      const fuchsia_hardware_display_ControllerSetLayerPrimaryConfigRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerPrimaryPosition(
      const fuchsia_hardware_display_ControllerSetLayerPrimaryPositionRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerPrimaryAlpha(
      const fuchsia_hardware_display_ControllerSetLayerPrimaryAlphaRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerCursorConfig(
      const fuchsia_hardware_display_ControllerSetLayerCursorConfigRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerCursorPosition(
      const fuchsia_hardware_display_ControllerSetLayerCursorPositionRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerColorConfig(
      const fuchsia_hardware_display_ControllerSetLayerColorConfigRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetLayerImage(const fuchsia_hardware_display_ControllerSetLayerImageRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleCheckConfig(const fuchsia_hardware_display_ControllerCheckConfigRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleApplyConfig(const fuchsia_hardware_display_ControllerApplyConfigRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleEnableVsync(const fuchsia_hardware_display_ControllerEnableVsyncRequest* req,
                         fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetVirtconMode(const fuchsia_hardware_display_ControllerSetVirtconModeRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleGetSingleBufferFramebuffer(
      const fuchsia_hardware_display_ControllerGetSingleBufferFramebufferRequest* req,
      fidl::Builder* resp_builder, zx_handle_t* handle_out, bool* has_handle_out,
      const fidl_type_t** resp_table);
  void HandleImportBufferCollection(
      const fuchsia_hardware_display_ControllerImportBufferCollectionRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleSetBufferCollectionConstraints(
      const fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleReleaseBufferCollection(
      const fuchsia_hardware_display_ControllerReleaseBufferCollectionRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);

  void HandleIsCaptureSupported(
      const fuchsia_hardware_display_ControllerIsCaptureSupportedRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);
  void HandleImportImageForCapture(
      const fuchsia_hardware_display_ControllerImportImageForCaptureRequest* req,
      fidl::Builder* resp_builder, const fidl_type_t** resp_table);

  void HandleStartCapture(const fuchsia_hardware_display_ControllerStartCaptureRequest* req,
                          fidl::Builder* resp_builder, const fidl_type_t** resp_table);

  void HandleReleaseCapture(const fuchsia_hardware_display_ControllerReleaseCaptureRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);

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

  zx::channel sysmem_allocator_;

  struct Collections {
    // Sent to the hardware driver.
    zx::channel driver;
    // If the VC is using this, |kernel| is the collection used for setting
    // it as kernel framebuffer.
    zx::channel kernel;
  };
  std::map<uint64_t, Collections> collection_map_;

  Fence::Map fences_ __TA_GUARDED(fence_mtx_);
  // Mutex held when creating or destroying fences.
  mtx_t fence_mtx_;

  Layer::Map layers_;
  uint64_t next_layer_id = 1;

  // TODO(stevensd): Delete this when client stop using SetDisplayImage
  uint64_t display_image_layer_ = INVALID_ID;

  void HandleControllerApi(async_dispatcher_t* dispatcher, async::WaitBase* self,
                           zx_status_t status, const zx_packet_signal_t* signal);
  async::WaitMethod<Client, &Client::HandleControllerApi> api_wait_{this};

  void NotifyDisplaysChanged(const int32_t* displays_added, uint32_t added_count,
                             const int32_t* displays_removed, uint32_t removed_count);
  bool CheckConfig(fidl::Builder* resp_builder);

  fbl::RefPtr<FenceReference> GetFence(uint64_t id) __TA_EXCLUDES(fence_mtx_);

  uint64_t GetActiveCaptureImage() { return current_capture_image_; }

  // Capture related book keeping
  uint64_t capture_fence_id_ = INVALID_ID;
  uint64_t current_capture_image_ = INVALID_ID;
  uint64_t pending_capture_release_image_ = INVALID_ID;
};

// ClientProxy manages interactions between its Client instance and the ddk and the
// controller. Methods on this class are thread safe.
using ClientParent = ddk::Device<ClientProxy, ddk::UnbindableNew, ddk::Closable>;
class ClientProxy : public ClientParent {
 public:
  // "client_id" is assigned by the Controller to distinguish clients.
  ClientProxy(Controller* controller, bool is_vc, uint32_t client_id);

  // This is used for testing
  ClientProxy(Controller* controller, bool is_vc, uint32_t client_id, zx::channel server_channel);

  ~ClientProxy();
  zx_status_t Init(zx::channel server_channel);

  zx_status_t DdkClose(uint32_t flags);
  void DdkUnbindNew(ddk::UnbindTxn txn);
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
    fbl::AutoLock lock(controller_->mtx());
    enable_vsync_ = enable;
  }

  void EnableCapture(bool enable) {
    ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);
    enable_capture_ = enable;
  }
  void OnClientDead();

  uint32_t id() const { return handler_.id(); }

  // This is used for testing
  void CloseTest();

 private:
  void CloseOnControllerLoop();

  Controller* controller_;
  bool is_vc_;
  zx::channel server_channel_;
  Client handler_;
  bool enable_vsync_ = false;
  bool enable_capture_ = false;

  // This variable is used to limit the number of errors logged in case of channel oom error
  static constexpr uint32_t kChannelOomPrintFreq = 600;  // 1 per 10 seconds (assuming 60fps)
  uint32_t chn_oom_print_freq_ = 0;
  uint64_t total_oom_errors_ = 0;
};

}  // namespace display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_CLIENT_H_

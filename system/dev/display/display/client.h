// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddk/protocol/display-controller.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/device/display-controller.h>
#include <zircon/listnode.h>

#include "controller.h"
#include "fence.h"
#include "fuchsia/display/c/fidl.h"
#include "id-map.h"
#include "image.h"

namespace display {

class Layer;
class Client;

typedef struct layer_node : public fbl::SinglyLinkedListable<layer_node*> {
    Layer* layer;
} layer_node_t;

// Almost-POD used by Client to manage layer state. Public state is used by Controller.
class Layer : public IdMappable<fbl::unique_ptr<Layer>> {
public:
    fbl::RefPtr<Image> current_image() const { return displayed_image_; };
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
    uint64_t pending_present_event_id_;
    uint64_t pending_signal_event_id_;

    // The image given to SetLayerImage which hasn't been applied yet.
    fbl::RefPtr<Image> pending_image_;

    // Image which are waiting to be displayed
    list_node_t waiting_images_ = LIST_INITIAL_VALUE(waiting_images_);
    // The image which has most recently been sent to the display controller impl
    fbl::RefPtr<Image> displayed_image_;

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
class DisplayConfig : public IdMappable<fbl::unique_ptr<DisplayConfig>> {
public:
    bool apply_layer_change() {
        bool ret = pending_apply_layer_change_;
        pending_apply_layer_change_ = false;
        return ret;
    }

    uint32_t vsync_layer_count() const { return vsync_layer_count_; }
    const display_config_t* current_config() const { return &current_; }
    const fbl::SinglyLinkedList<layer_node_t*>& get_current_layers() const {
        return current_layers_;
    }

private:
    display_config_t current_;
    display_config_t pending_;

    bool pending_layer_change_;
    bool pending_apply_layer_change_;
    fbl::SinglyLinkedList<layer_node_t*> pending_layers_;
    fbl::SinglyLinkedList<layer_node_t*> current_layers_;

    fbl::unique_ptr<zx_pixel_format_t[]> pixel_formats_;
    uint32_t pixel_format_count_;

    fbl::unique_ptr<cursor_info_t[]> cursor_infos_;
    uint32_t cursor_info_count_;

    uint32_t vsync_layer_count_;
    bool display_config_change_ = false;

    friend Client;
    friend ClientProxy;
};

// The Client class manages all state associated with an open display client
// connection. Over than initialization, all methods of this class execute on
// on the controller's looper, so no synchronization is necessary.
class Client : private FenceCallback {
public:
    Client(Controller* controller, ClientProxy* proxy, bool is_vc);
    ~Client();
    zx_status_t Init(zx_handle_t server_handle);

    void OnDisplaysChanged(uint64_t* displays_added,
                           uint32_t added_count,
                           uint64_t* displays_removed,
                           uint32_t removed_count);
    void SetOwnership(bool is_owner);
    void ApplyConfig();

    void OnFenceFired(FenceReference* fence) override;
    void OnRefForFenceDead(Fence* fence) override;

    void TearDown();

    bool IsValid() { return server_handle_ != ZX_HANDLE_INVALID; }
private:
    void HandleImportVmoImage(const fuchsia_display_ControllerImportVmoImageRequest* req,
                              fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleReleaseImage(const fuchsia_display_ControllerReleaseImageRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleImportEvent(const fuchsia_display_ControllerImportEventRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleReleaseEvent(const fuchsia_display_ControllerReleaseEventRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetDisplayImage(const fuchsia_display_ControllerSetDisplayImageRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleCreateLayer(const fuchsia_display_ControllerCreateLayerRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleDestroyLayer(const fuchsia_display_ControllerDestroyLayerRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetDisplayMode(const fuchsia_display_ControllerSetDisplayModeRequest* req,
                              fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetDisplayColorConversion(
            const fuchsia_display_ControllerSetDisplayColorConversionRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetDisplayLayers(const fuchsia_display_ControllerSetDisplayLayersRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerPrimaryConfig(
            const fuchsia_display_ControllerSetLayerPrimaryConfigRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerPrimaryPosition(
            const fuchsia_display_ControllerSetLayerPrimaryPositionRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerPrimaryAlpha(
            const fuchsia_display_ControllerSetLayerPrimaryAlphaRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerCursorConfig(
            const fuchsia_display_ControllerSetLayerCursorConfigRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerCursorPosition(
            const fuchsia_display_ControllerSetLayerCursorPositionRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerColorConfig(
            const fuchsia_display_ControllerSetLayerColorConfigRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetLayerImageLegacy(uint64_t layer_id, uint64_t image_id,
                                   uint64_t wait_event_id, uint64_t present_event_id,
                                   uint64_t signal_event_id);
    void HandleSetLayerImage(const fuchsia_display_ControllerSetLayerImageRequest* req,
                             fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleCheckConfig(const fuchsia_display_ControllerCheckConfigRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleApplyConfig(const fuchsia_display_ControllerApplyConfigRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleEnableVsync(const fuchsia_display_ControllerEnableVsyncRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetVirtconMode(const fuchsia_display_ControllerSetVirtconModeRequest* req,
                              fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleComputeLinearImageStride(
            const fuchsia_display_ControllerComputeLinearImageStrideRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleAllocateVmo(const fuchsia_display_ControllerAllocateVmoRequest* req,
                           fidl::Builder* resp_builder,
                           zx_handle_t* handle_out, bool* has_handle_out,
                           const fidl_type_t** resp_table);

    zx_status_t CreateLayer(uint64_t* layer_id);

    // Cleans up layer state associated with image id. If id == INVALID_ID, then
    // cleans up all image layer state. Return true if a current layer was modified.
    bool CleanUpImageLayerState(uint64_t id);

    Controller* controller_;
    ClientProxy* proxy_;
    bool is_vc_;
    uint64_t console_fb_display_id_ = -1;

    zx_handle_t server_handle_;
    uint64_t next_image_id_ = 1; // Only INVALID_ID == 0 is invalid

    Image::Map images_;
    DisplayConfig::Map configs_;
    bool pending_config_valid_ = false;
    bool is_owner_ = false;
    // A counter for the number of times the client has successfully applied
    // a configuration. This does not account for changes due to waiting images.
    uint32_t client_apply_count_ = 0;

    Fence::Map fences_ __TA_GUARDED(fence_mtx_);
    // Mutex held when creating or destroying fences.
    mtx_t fence_mtx_;

    Layer::Map layers_;
    uint64_t next_layer_id = 1;

    // TODO(stevensd): Delete this when client stop using SetDisplayImage
    uint64_t display_image_layer_ = INVALID_ID;

    void HandleControllerApi(async_t* async, async::WaitBase* self,
                             zx_status_t status, const zx_packet_signal_t* signal);
    async::WaitMethod<Client, &Client::HandleControllerApi> api_wait_{this};

    void NotifyDisplaysChanged(const int32_t* displays_added, uint32_t added_count,
                               const int32_t* displays_removed, uint32_t removed_count);
    bool CheckConfig(fidl::Builder* resp_builder);

    fbl::RefPtr<FenceReference> GetFence(uint64_t id);
};

// ClientProxy manages interactions between its Client instance and the ddk and the
// controller. Methods on this class are thread safe.
using ClientParent = ddk::Device<ClientProxy, ddk::Ioctlable, ddk::Closable>;
class ClientProxy : public ClientParent {
public:
    ClientProxy(Controller* controller, bool is_vc);
    ~ClientProxy();
    zx_status_t Init();

    zx_status_t DdkClose(uint32_t flags);
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* actual);
    void DdkRelease();

    // Requires holding controller_->mtx() lock
    void OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                        uint64_t* image_ids, uint32_t count);
    zx_status_t OnDisplaysChanged(const uint64_t* displays_added, uint32_t added_count,
                                  const uint64_t* displays_removed, uint32_t removed_count);
    void SetOwnership(bool is_owner);
    void ReapplyConfig();

    // Requires holding controller_->mtx() lock
    void EnableVsync(bool enable) {
        ZX_DEBUG_ASSERT(mtx_trylock(controller_->mtx()) == thrd_busy);

        enable_vsync_ = enable;
    }
    void OnClientDead();
    void Close();
private:
    Controller* controller_;
    bool is_vc_;
    Client handler_;
    bool enable_vsync_ = false;

    zx::channel server_handle_;
    zx::channel client_handle_;
};

} // namespace display

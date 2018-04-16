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

#include "controller.h"
#include "display/c/fidl.h"
#include "fence.h"
#include "id-map.h"
#include "image.h"

namespace display {

class DisplayConfig : public IdMappable<fbl::unique_ptr<DisplayConfig>> {
public:
    display_config_t current;
    display_config_t pending;

    int32_t pending_wait_event_id;
    int32_t pending_present_event_id;
    int32_t pending_signal_event_id;

    // The image given to SetDisplayImage which hasn't been applied yet.
    bool has_pending_image;
    fbl::RefPtr<Image> pending_image;

    // Image which are waiting to be displayed
    fbl::DoublyLinkedList<fbl::RefPtr<Image>> waiting_images;
    // The image which has most recently been sent to the display controller impl
    fbl::RefPtr<Image> displayed_image;

    fbl::unique_ptr<zx_pixel_format_t[]> pixel_formats;
    uint32_t pixel_format_count;
};

// The Client class manages all state associated with an open display client
// connection. Over than initialization, all methods of this class execute on
// on the controller's looper, so no synchronization is necessary.
class Client : private FenceCallback {
public:
    Client(Controller* controller, ClientProxy* proxy, bool is_vc);
    ~Client();

    zx_status_t InitApiConnection(zx::handle* client_handle);

    void OnDisplaysChanged(fbl::unique_ptr<DisplayConfig>* displays_added,
                           uint32_t added_count,
                           int32_t* displays_removed,
                           uint32_t removed_count);
    void SetOwnership(bool is_owner);

    void OnFenceFired(FenceReference* fence) override;
    void OnRefForFenceDead(Fence* fence) override;

    // Resets state associated with the remote client. Note that this does not fully reset
    // the class, as the Client instance can get re-inited with InitApiConnection.
    void Reset();
private:
    void HandleSetControllerCallback(const display_ControllerSetControllerCallbackRequest* req,
                                     fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleImportVmoImage(const display_ControllerImportVmoImageRequest* req,
                              fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleReleaseImage(const display_ControllerReleaseImageRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleImportEvent(const display_ControllerImportEventRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleReleaseEvent(const display_ControllerReleaseEventRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetDisplayImage(const display_ControllerSetDisplayImageRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleCheckConfig(const display_ControllerCheckConfigRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleApplyConfig(const display_ControllerApplyConfigRequest* req,
                           fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleSetOwnership(const display_ControllerSetOwnershipRequest* req,
                            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleComputeLinearImageStride(
            const display_ControllerComputeLinearImageStrideRequest* req,
            fidl::Builder* resp_builder, const fidl_type_t** resp_table);
    void HandleAllocateVmo(const display_ControllerAllocateVmoRequest* req,
                           fidl::Builder* resp_builder,
                           zx_handle_t* handle_out, bool* has_handle_out,
                           const fidl_type_t** resp_table);

    Controller* controller_;
    ClientProxy* proxy_;
    bool is_vc_;

    zx::channel server_handle_;
    zx::channel callback_handle_;
    int32_t next_txn_ = 0x0;
    int32_t next_image_id_ = 0x0;

    Image::Map images_;
    DisplayConfig::Map configs_;
    bool pending_config_valid_ = false;
    bool is_owner_ = false;
    bool config_applied_ = false;

    Fence::Map fences_ __TA_GUARDED(fence_mtx_);
    // Mutex held when creating or destroying fences.
    mtx_t fence_mtx_;

    void HandleControllerApi(async_t* async, async::WaitBase* self,
                             zx_status_t status, const zx_packet_signal_t* signal);
    async::WaitMethod<Client, &Client::HandleControllerApi> api_wait_{this};

    void NotifyDisplaysChanged(const int32_t* displays_added, uint32_t added_count,
                               const int32_t* displays_removed, uint32_t removed_count);
    void ApplyConfig();
    bool CheckConfig();

    fbl::RefPtr<FenceReference> GetFence(int32_t id);
};

// ClientProxy manages interactions between its Client instance and the ddk and the
// controller. Methods on this class are thread safe.
using ClientParent = ddk::Device<ClientProxy, ddk::Ioctlable, ddk::Closable>;
class ClientProxy : public ClientParent {
public:
    ClientProxy(Controller* controller, bool is_vc);
    ~ClientProxy();

    zx_status_t DdkClose(uint32_t flags);
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* actual);
    void DdkRelease();

    zx_status_t OnDisplaysChanged(const DisplayInfo** displays_added, uint32_t added_count,
                                  const int32_t* displays_removed, uint32_t removed_count);
    void SetOwnership(bool is_owner);

    void OnClientDead();
    void Close();
private:
    Controller* controller_;
    bool is_vc_;
    Client handler_;

    // Various state for tracking the connection to the client.
    // TODO(stevensd): Simplify this when ioctls are replaced with direct FIDL apis and
    // when FIDL supports events so the two channels are merged.
    mtx_t bind_lock_;
    bool bound_ __TA_GUARDED(bind_lock_) = false;
    bool closed_ __TA_GUARDED(bind_lock_) = false;
};

} // namespace display

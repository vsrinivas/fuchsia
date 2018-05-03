// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddk/protocol/display-controller.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/edid/edid.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include "id-map.h"
#include "image.h"

namespace display {

class ClientProxy;
class Controller;
class DisplayConfig;

class DisplayInfo : public IdMappable<fbl::unique_ptr<DisplayInfo>> {
public:
    display_info_t info;

    // TODO(stevensd): extract a list of all valid timings
    edid::timing_params_t preferred_timing;
    fbl::DoublyLinkedList<fbl::RefPtr<Image>> images;
};

using ControllerParent = ddk::Device<Controller, ddk::Unbindable, ddk::Openable, ddk::OpenAtable>;
class Controller : public ControllerParent,
                   public ddk::EmptyProtocol<ZX_PROTOCOL_DISPLAY_CONTROLLER> {
public:
    Controller(zx_device_t* parent);

    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();
    zx_status_t Bind(fbl::unique_ptr<display::Controller>* device_ptr);

    void OnDisplaysChanged(int32_t* displays_added, uint32_t added_count,
                           int32_t* displays_removed, uint32_t removed_count);
    void OnDisplayVsync(int32_t display_id, void* handles);
    void OnClientDead(ClientProxy* client);
    void SetVcOwner(bool vc_is_owner);
    void ShowActiveDisplay();

    void OnConfigApplied(DisplayConfig* configs[], int32_t count);

    void ReleaseImage(Image* image);

    display_controller_protocol_ops_t* ops() { return ops_.ops; }
    void* ops_ctx() { return ops_.ctx; }
    async::Loop& loop() { return loop_; }
    bool current_thread_is_loop() { return thrd_current() == loop_thread_; }
    mtx_t* mtx() { return &mtx_; }
private:
    void HandleClientOwnershipChanges() __TA_REQUIRES(mtx_);

    // mtx_ is a global lock on state shared among clients.
    mtx_t mtx_;

    DisplayInfo::Map displays_ __TA_GUARDED(mtx_);

    ClientProxy* vc_client_ __TA_GUARDED(mtx_) = nullptr;
    ClientProxy* primary_client_ __TA_GUARDED(mtx_) = nullptr;
    bool vc_is_owner_ __TA_GUARDED(mtx_);
    ClientProxy* active_client_ __TA_GUARDED(mtx_) = nullptr;

    async::Loop loop_;
    thrd_t loop_thread_;
    display_controller_protocol_t ops_;
};

} // namespace display

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t display_controller_bind(void* ctx, zx_device_t* parent);
__END_CDECLS

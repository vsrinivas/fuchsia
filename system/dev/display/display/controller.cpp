// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/display-controller.h>

#include "controller.h"
#include "client.h"
#include "display/c/fidl.h"

namespace {

void on_displays_changed(void* ctx, int32_t* displays_added, uint32_t added_count,
                         int32_t* displays_removed, uint32_t removed_count) {
    static_cast<display::Controller*>(ctx)->OnDisplaysChanged(
            displays_added, added_count, displays_removed, removed_count);
}

void on_display_vsync(void* ctx, int32_t display, void* handle) {
    static_cast<display::Controller*>(ctx)->OnDisplayVsync(display, handle);
}

display_controller_cb_t dc_cb = {
    .on_displays_changed = on_displays_changed,
    .on_display_vsync = on_display_vsync,
};

} // namespace

namespace display {

void Controller::OnDisplaysChanged(int32_t* displays_added, uint32_t added_count,
                                   int32_t* displays_removed, uint32_t removed_count) {
    const DisplayInfo* added_success[added_count];
    int32_t added_success_count = 0;
    int32_t removed_success[removed_count];
    int32_t removed_success_count = 0;

    fbl::AutoLock lock(&mtx_);
    for (unsigned i = 0; i < removed_count; i++) {
        auto target = displays_.erase(displays_removed[i]);
        if (target) {
            removed_success[removed_success_count++] = displays_removed[i];
        } else {
            zxlogf(TRACE, "Unknown display %d removed\n", displays_removed[i]);
        }
    }

    for (unsigned i = 0; i < added_count; i++) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<DisplayInfo> info = fbl::make_unique_checked<DisplayInfo>(&ac);
        if (!ac.check()) {
            zxlogf(INFO, "Out of memory when processing display hotplug\n");
            break;
        }

        info->id = displays_added[i];
        if (ops_.ops->get_display_info(ops_.ctx, info->id, &info->info) != ZX_OK) {
            zxlogf(TRACE, "Error getting display info for %d\n", info->id);
            continue;
        }
        edid::Edid edid;
        const char* edid_err = "No preferred timing";
        if (!edid.Init(info->info.edid, info->info.edid_length, &edid_err)
                || !edid.GetPreferredTiming(&info->preferred_timing)) {
            zxlogf(TRACE, "Failed to parse edid \"%s\"\n", edid_err);
            continue;
        }

        auto info_ptr = info.get();
        if (displays_.insert_or_find(fbl::move(info))) {
            added_success[added_success_count++] = info_ptr;
        } else {
            zxlogf(INFO, "Ignoring duplicate display\n");
        }
    }

    zx_status_t status;
    if (active_client_) {
        status = active_client_->OnDisplaysChanged(added_success, added_success_count,
                                                   removed_success, removed_success_count);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error when processing hotplug (%d)\n", status);
        }
    }
}

void Controller::OnDisplayVsync(int32_t display_id, void* handle) {
}

void Controller::OnClientClosed(ClientProxy* client) {
    fbl::AutoLock lock(&mtx_);
    active_client_ = nullptr;
}

zx_status_t Controller::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    return DdkOpenAt(dev_out, "", flags);
}

zx_status_t Controller::DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags) {
    fbl::AutoLock lock(&mtx_);

    if (active_client_) {
        zxlogf(TRACE, "Already bound\n");
        return ZX_ERR_ALREADY_BOUND;
    }

    fbl::AllocChecker ac;
    auto client = fbl::make_unique_checked<ClientProxy>(&ac, this);
    if (!ac.check()) {
        zxlogf(TRACE, "Failed to alloc client\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Add all existing displays to the client
    for (const DisplayInfo& display : displays_) {
        const DisplayInfo* info = &display;
        zx_status_t status = client->OnDisplaysChanged(&info, 1, nullptr, 0);
        if (status != ZX_OK) {
            zxlogf(TRACE, "Failed to init client %d\n", status);
            return status;
        }
    }

    zx_status_t status;
    if ((status = client->DdkAdd("dc", DEVICE_ADD_INSTANCE)) != ZX_OK) {
        zxlogf(TRACE, "Failed to add client %d\n", status);
        return status;
    }

    ClientProxy* client_ptr = client.release();
    *dev_out = client_ptr->zxdev();

    zxlogf(TRACE, "New client connected at \"%s\"\n", path);

    active_client_ = client_ptr;

    return ZX_OK;
}

zx_status_t Controller::Bind(fbl::unique_ptr<display::Controller>* device_ptr) {
    zx_status_t status;
    if (device_get_protocol(parent_, ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL, &ops_)) {
        ZX_DEBUG_ASSERT_MSG(false, "Display controller bind mismatch");
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = loop_.StartThread("display-client-loop", &loop_thread_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to start loop %d\n", status);
        return status;
    }

    if ((status = DdkAdd("display-controller")) != ZX_OK) {
        zxlogf(ERROR, "Failed to add display core device %d\n", status);
        return status;
    }
    __UNUSED auto ptr = device_ptr->release();

    ops_.ops->set_display_controller_cb(ops_.ctx, this, &dc_cb);

    return ZX_OK;
}

void Controller::DdkUnbind() {
    {
        fbl::AutoLock lock(&mtx_);
        if (active_client_) {
            active_client_->Close();
        }
    }
    DdkRemove();
}

void Controller::DdkRelease() {
    delete this;
}

Controller::Controller(zx_device_t* parent) : ControllerParent(parent),
        active_client_(nullptr) {
    mtx_init(&mtx_, mtx_plain);
}

// ControllerInstance methods

} // namespace display

zx_status_t display_controller_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<display::Controller> core(new (&ac) display::Controller(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return core->Bind(&core);
}

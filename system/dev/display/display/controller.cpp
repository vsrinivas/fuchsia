// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/display-controller.h>

#include "controller.h"
#include "client.h"
#include "fuchsia/display/c/fidl.h"

namespace {

void on_displays_changed(void* ctx, uint64_t* displays_added, uint32_t added_count,
                         uint64_t* displays_removed, uint32_t removed_count) {
    static_cast<display::Controller*>(ctx)->OnDisplaysChanged(
            displays_added, added_count, displays_removed, removed_count);
}

void on_display_vsync(void* ctx, uint64_t display, void* handle) {
    static_cast<display::Controller*>(ctx)->OnDisplayVsync(display, handle);
}

display_controller_cb_t dc_cb = {
    .on_displays_changed = on_displays_changed,
    .on_display_vsync = on_display_vsync,
};

} // namespace

namespace display {

void Controller::OnDisplaysChanged(uint64_t* displays_added, uint32_t added_count,
                                   uint64_t* displays_removed, uint32_t removed_count) {
    const DisplayInfo* added_success[added_count];
    int32_t added_success_count = 0;
    uint64_t removed_success[removed_count];
    int32_t removed_success_count = 0;

    fbl::AutoLock lock(&mtx_);
    for (unsigned i = 0; i < removed_count; i++) {
        auto target = displays_.erase(displays_removed[i]);
        if (target) {
            removed_success[removed_success_count++] = displays_removed[i];

            while (!target->images.is_empty()) {
                auto image = target->images.pop_front();
                image->StartRetire();
                image->OnRetire();
            }
        } else {
            zxlogf(TRACE, "Unknown display %ld removed\n", displays_removed[i]);
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
            zxlogf(TRACE, "Error getting display info for %ld\n", info->id);
            continue;
        }
        if (info->info.edid_present) {
            edid::Edid edid;
            const char* edid_err = "No preferred timing";
            if (!edid.Init(info->info.panel.edid.data, info->info.panel.edid.length, &edid_err)
                    || !edid.GetPreferredTiming(&info->preferred_timing)) {
                zxlogf(TRACE, "Failed to parse edid \"%s\"\n", edid_err);
                continue;
            }
        }

        auto info_ptr = info.get();
        if (displays_.insert_or_find(fbl::move(info))) {
            added_success[added_success_count++] = info_ptr;
        } else {
            zxlogf(INFO, "Ignoring duplicate display\n");
        }
    }

    zx_status_t status;
    if (vc_client_) {
        status = vc_client_->OnDisplaysChanged(added_success, added_success_count,
                                               removed_success, removed_success_count);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error when processing hotplug (%d)\n", status);
        }
    }
    if (primary_client_) {
        status = primary_client_->OnDisplaysChanged(added_success, added_success_count,
                                                    removed_success, removed_success_count);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error when processing hotplug (%d)\n", status);
        }
    }
}

void Controller::OnDisplayVsync(uint64_t display_id, void* handle) {
    fbl::AutoLock lock(&mtx_);
    fbl::DoublyLinkedList<fbl::RefPtr<Image>>* images = nullptr;
    for (auto& display_config : displays_) {
        if (display_config.id == display_id) {
            images = &display_config.images;
            break;
        }
    }

    if (images) {
        while (!images->is_empty()) {
            auto& image = images->front();
            image.OnPresent();
            if (image.info().handle == handle) {
                break;
            } else {
                image.OnRetire();
                images->pop_front();
            }
        }
    }
}

void Controller::OnConfigApplied(DisplayConfig* configs[], int32_t count) {
    fbl::AutoLock lock(&mtx_);
    for (int i = 0; i < count; i++) {
        auto* config = configs[i];
        if (config->displayed_image) {
            auto display = displays_.find(config->id);
            if (display.IsValid()) {
                // This can happen if we reapply a display's current configuration or if
                // we switch display owners rapidly. The fact that this is being put back in
                // the queue means we don't want to retire the image yet. So at worst this
                // will delay the image's retire by a few frames.
                if (static_cast<fbl::DoublyLinkedListable<fbl::RefPtr<Image>>*>(
                            config->displayed_image.get())->InContainer()) {
                    display->images.erase(*config->displayed_image);
                } else {
                    config->displayed_image->StartPresent();
                }
                display->images.push_back(config->displayed_image);
            }
        }
    }
}

void Controller::ReleaseImage(Image* image) {
    ops_.ops->release_image(ops_.ctx, &image->info());
}

void Controller::SetVcOwner(bool vc_is_owner) {
    fbl::AutoLock lock(&mtx_);
    vc_is_owner_ = vc_is_owner;
    HandleClientOwnershipChanges();
}

void Controller::HandleClientOwnershipChanges() {
    ClientProxy* new_active;
    if (vc_is_owner_ || primary_client_ == nullptr) {
        new_active = vc_client_;
    } else {
        new_active = primary_client_;
    }

    if (new_active != active_client_) {
        if (active_client_) {
            active_client_->SetOwnership(false);
        }
        if (new_active) {
            new_active->SetOwnership(true);
        }
        active_client_ = new_active;
    }
}

void Controller::OnClientDead(ClientProxy* client) {
    fbl::AutoLock lock(&mtx_);
    if (client == vc_client_) {
        vc_client_ = nullptr;
        vc_is_owner_ = false;
    } else if (client == primary_client_) {
        primary_client_ = nullptr;
    }
    HandleClientOwnershipChanges();
}

zx_status_t Controller::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    return DdkOpenAt(dev_out, "", flags);
}

zx_status_t Controller::DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags) {
    fbl::AutoLock lock(&mtx_);

    bool is_vc = strcmp("virtcon", path) == 0;
    if ((is_vc && vc_client_) || (!is_vc && primary_client_)) {
        zxlogf(TRACE, "Already bound\n");
        return ZX_ERR_ALREADY_BOUND;
    }

    fbl::AllocChecker ac;
    auto client = fbl::make_unique_checked<ClientProxy>(&ac, this, is_vc);
    if (!ac.check()) {
        zxlogf(TRACE, "Failed to alloc client\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = client->Init();
    if (status != ZX_OK) {
        zxlogf(TRACE, "Failed to init client %d\n", status);
        return status;
    }

    // Add all existing displays to the client
    if (displays_.size() > 0) {
        const DisplayInfo* current_displays[displays_.size()];
        int idx = 0;
        for (const DisplayInfo& display : displays_) {
            current_displays[idx++] = &display;
        }
        if ((status = client->OnDisplaysChanged(current_displays, idx, nullptr, 0)) != ZX_OK) {
            zxlogf(TRACE, "Failed to init client %d\n", status);
            return status;
        }
    }

    if ((status = client->DdkAdd(is_vc ? "dc-vc" : "dc", DEVICE_ADD_INSTANCE)) != ZX_OK) {
        zxlogf(TRACE, "Failed to add client %d\n", status);
        return status;
    }

    ClientProxy* client_ptr = client.release();
    *dev_out = client_ptr->zxdev();

    zxlogf(TRACE, "New client connected at \"%s\"\n", path);

    if (is_vc) {
        vc_client_ = client_ptr;
    } else {
        primary_client_ = client_ptr;
    }
    HandleClientOwnershipChanges();

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
        if (vc_client_) {
            vc_client_->Close();
        }
        if (primary_client_) {
            primary_client_->Close();
        }
    }
    DdkRemove();
}

void Controller::DdkRelease() {
    delete this;
}

Controller::Controller(zx_device_t* parent) : ControllerParent(parent) {
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

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <lib/edid/edid.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/display-controller.h>

#include "client.h"

#define DC_IMPL_CALL(fn, ...) controller_->ops()->fn(controller_->ops_ctx(), __VA_ARGS__)
#define SELECT_TABLE_CASE(NAME) case NAME ## Ordinal: table = &NAME ## RequestTable; break
#define HANDLE_REQUEST_CASE(NAME) \
    case display_Controller ## NAME ## Ordinal: { \
        auto req = reinterpret_cast<const display_Controller ## NAME ## Request*>(msg.bytes().data()); \
        Handle ## NAME (req, &builder, &out_type); \
        break; \
    }

namespace {

zx_status_t decode_message(fidl::Message* msg) {
    zx_status_t res;
    const fidl_type_t* table = nullptr;
    switch (msg->ordinal()) {
    SELECT_TABLE_CASE(display_ControllerSetControllerCallback);
    SELECT_TABLE_CASE(display_ControllerImportVmoImage);
    SELECT_TABLE_CASE(display_ControllerReleaseImage);
    SELECT_TABLE_CASE(display_ControllerImportEvent);
    SELECT_TABLE_CASE(display_ControllerReleaseEvent);
    SELECT_TABLE_CASE(display_ControllerSetDisplayImage);
    SELECT_TABLE_CASE(display_ControllerCheckConfig);
    SELECT_TABLE_CASE(display_ControllerApplyConfig);
    SELECT_TABLE_CASE(display_ControllerSetOwnership);
    SELECT_TABLE_CASE(display_ControllerComputeLinearImageStride);
    SELECT_TABLE_CASE(display_ControllerAllocateVmo);
    }
    if (table != nullptr) {
        const char* err;
        if ((res = msg->Decode(table, &err)) != ZX_OK) {
            zxlogf(INFO, "Error decoding message %d: %s\n", msg->ordinal(), err);
        }
    } else {
        zxlogf(INFO, "Unknown fidl ordinal %d\n", msg->ordinal());
        res = ZX_ERR_NOT_SUPPORTED;
    }
    return res;
}

} // namespace

namespace display {

void Client::HandleControllerApi(async_t* async, async::WaitBase* self,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        zxlogf(INFO, "Unexpected status async status %d\n", status);
        ZX_DEBUG_ASSERT(false);
        return;
    } else if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        zxlogf(TRACE, "Client closed\n");
        Reset();
        return;
    }

    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_READABLE);

    zx_handle_t in_handle;
    uint8_t in_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Message msg(fidl::BytePart(in_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
                      fidl::HandlePart(&in_handle, 1));
    status = msg.Read(server_handle_.get(), 0);
    api_wait_.Begin(controller_->loop().async());

    if (status != ZX_OK) {
        zxlogf(TRACE, "Channel read failed %d\n", status);
        return;
    } else if ((status = decode_message(&msg)) != ZX_OK) {
        return;
    }

    uint8_t out_byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(out_byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);
    zx_handle_t out_handle = ZX_HANDLE_INVALID;
    bool has_out_handle = false;
    const fidl_type_t* out_type = nullptr;

    switch (msg.ordinal()) {
    HANDLE_REQUEST_CASE(SetControllerCallback);
    HANDLE_REQUEST_CASE(ImportVmoImage);
    HANDLE_REQUEST_CASE(ReleaseImage);
    HANDLE_REQUEST_CASE(ImportEvent);
    HANDLE_REQUEST_CASE(ReleaseEvent);
    HANDLE_REQUEST_CASE(SetDisplayImage);
    HANDLE_REQUEST_CASE(CheckConfig);
    HANDLE_REQUEST_CASE(ApplyConfig);
    HANDLE_REQUEST_CASE(SetOwnership);
    HANDLE_REQUEST_CASE(ComputeLinearImageStride);
    case display_ControllerAllocateVmoOrdinal: {
        auto r = reinterpret_cast<const display_ControllerAllocateVmoRequest*>(msg.bytes().data());
        HandleAllocateVmo(r, &builder, &out_handle, &has_out_handle, &out_type);
        break;
    }
    default:
        zxlogf(INFO, "Unknown ordinal %d\n", msg.ordinal());
    }

    fidl::BytePart resp_bytes = builder.Finalize();
    if (resp_bytes.actual() != 0) {
        ZX_DEBUG_ASSERT(out_type != nullptr);

        fidl::Message resp(fbl::move(resp_bytes),
                           fidl::HandlePart(&out_handle, 1, has_out_handle ? 1 : 0));
        resp.header() = msg.header();

        const char* err_msg;
        ZX_DEBUG_ASSERT_MSG(resp.Validate(out_type, &err_msg) == ZX_OK,
                            "Error validating fidl response \"%s\"\n", err_msg);
        if ((status = resp.Write(server_handle_.get(), 0)) != ZX_OK) {
            zxlogf(INFO, "Error writing response message %d\n", status);
        }
    }
}

void Client::HandleSetControllerCallback(const display_ControllerSetControllerCallbackRequest* req,
                                         fidl::Builder* resp_builder,
                                         const fidl_type_t** resp_table) {
    callback_handle_.reset(req->callback);

    // Send notifications for the currently connected displays
    int32_t added[configs_.size()];
    auto iter = configs_.begin();
    for (unsigned i = 0; i < configs_.size(); i++, iter++) {
        added[i] = iter->id;
    }
    NotifyDisplaysChanged(added, static_cast<uint32_t>(configs_.size()), nullptr, 0);

    if (is_owner_) {
        display_ControllerCallbackOnClientOwnershipChangeRequest msg;
        msg.hdr.txid = next_txn_++;
        msg.hdr.ordinal = display_ControllerCallbackOnClientOwnershipChangeOrdinal;
        msg.has_ownership = true;

        zx_status_t status = callback_handle_.write(0, &msg, sizeof(msg), nullptr, 0);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error writing ownership message message %d\n", status);
        }
    }
}

void Client::HandleImportVmoImage(const display_ControllerImportVmoImageRequest* req,
                                  fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<display_ControllerImportVmoImageResponse>();
    *resp_table = &display_ControllerImportVmoImageResponseTable;

    zx::vmo vmo(req->vmo);

    // Clients are probably doing something horribly wrong if we've overflowed image
    // ids, so just fail when that happens.
    if (next_image_id_ < 0) {
        resp->res = ZX_ERR_NO_RESOURCES;
        return;
    }

    image_t dc_image;
    dc_image.height = req->image_config.height;
    dc_image.width = req->image_config.width;
    dc_image.pixel_format = req->image_config.pixel_format;
    dc_image.type = req->image_config.type;
    resp->res = DC_IMPL_CALL(import_vmo_image, &dc_image, vmo.get(), req->offset);

    if (resp->res == ZX_OK) {
        fbl::AllocChecker ac;
        auto image = fbl::AdoptRef(new (&ac) Image(controller_, dc_image, fbl::move(vmo)));
        if (!ac.check()) {
            DC_IMPL_CALL(release_image, &dc_image);

            resp->res = ZX_ERR_NO_MEMORY;
            return;
        }

        image->id = next_image_id_++;
        resp->image_id = image->id;
        images_.insert(fbl::move(image));
    }
}

void Client::HandleReleaseImage(const display_ControllerReleaseImageRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto image = images_.erase(req->image_id);
    if (!image) {
        return;
    }

    bool current_config_change = false;
    for (auto& config : configs_) {
        if (config.pending_image == image) {
            config.pending_image->DiscardAcquire();
            config.pending_image = nullptr;
            config.has_pending_image = false;
        }
        for (auto& waiting : config.waiting_images) {
            if (waiting.id == image->id) {
                config.waiting_images.erase(waiting);
                image->EarlyRetire();
                break;
            }
        }
        if (config.displayed_image == image) {
            {
                fbl::AutoLock lock(controller_->mtx());
                config.displayed_image->StartRetire();
            }

            config.displayed_image = nullptr;
            current_config_change = true;
            config_applied_ = false;
        }
    }

    if (is_owner_ && current_config_change) {
        ApplyConfig();
    }
}

void Client::HandleImportEvent(const display_ControllerImportEventRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    zx::event event(req->event);
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    bool success = false;

    fbl::AutoLock lock(&fence_mtx_);

    // TODO(stevensd): it would be good for this not to be able to fail due to allocation failures
    if (req->id >= 0) {
        auto fence = fences_.find(req->id);
        if (!fence.IsValid()) {
            fbl::AllocChecker ac;
            auto new_fence = fbl::AdoptRef(new (&ac) Fence(this, controller_->loop().async(),
                                                           req->id, fbl::move(event)));
            if (ac.check() && new_fence->CreateRef()) {
                fences_.insert_or_find(fbl::move(new_fence));
                success = true;
            }
        } else {
            success = fence->CreateRef();
        }
    }

    if (!success) {
        zxlogf(ERROR, "Failed to import event#%d (%d)\n", req->id, status);
        Reset();
    }
}

void Client::HandleReleaseEvent(const display_ControllerReleaseEventRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    // Hold a ref to prevent double locking if this destroys the fence.
    auto fence_ref = GetFence(req->id);
    if (fence_ref) {
        fbl::AutoLock lock(&fence_mtx_);
        fences_.find(req->id)->ClearRef();
    }
}

void Client::HandleSetDisplayImage(const display_ControllerSetDisplayImageRequest* req,
                                   fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto config = configs_.find(req->display);
    if (!config.IsValid()) {
        zxlogf(INFO, "SetDisplayImage ordinal with invalid display\n");
        return;
    }
    if (req->image_id < 0) {
        config->has_pending_image = true;
        config->pending_image = nullptr;
        pending_config_valid_ = false;
    } else {
        auto image = images_.find(req->image_id);
        if (image.IsValid() && image->Acquire()) {
            config->has_pending_image = true;

            config->pending_image = image.CopyPointer();
            config->pending_wait_event_id = req->wait_event_id;
            config->pending_present_event_id = req->present_event_id;
            config->pending_signal_event_id = req->signal_event_id;

            pending_config_valid_ = false;
        } else {
            zxlogf(INFO, "SetDisplayImage with %s image\n", image.IsValid() ? "invl" : "busy");
        }
    }
}

void Client::HandleCheckConfig(const display_ControllerCheckConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<display_ControllerCheckConfigResponse>();
    *resp_table = &display_ControllerCheckConfigResponseTable;

    pending_config_valid_ = CheckConfig();

    resp->valid = pending_config_valid_;

    if (req->discard) {
        for (auto& config : configs_) {
            config.pending_image->DiscardAcquire();
            config.pending_image = nullptr;
            config.has_pending_image = false;
        }
        pending_config_valid_ = true;
    }
}

void Client::HandleApplyConfig(const display_ControllerApplyConfigRequest* req,
                               fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    if (!pending_config_valid_) {
        pending_config_valid_ = CheckConfig();
        if (!pending_config_valid_) {
            zxlogf(INFO, "Tried to apply invalid config\n");
            return;
        }
    }

    for (auto& display_config : configs_) {
        if (!display_config.has_pending_image) {
            continue;
        }

        if (display_config.pending_image) {
            display_config.pending_image->PrepareFences(
                    GetFence(display_config.pending_wait_event_id),
                    GetFence(display_config.pending_present_event_id),
                    GetFence(display_config.pending_signal_event_id));
            display_config.waiting_images.push_back(fbl::move(display_config.pending_image));
        } else {
            // We're setting a blank image which is immedately ready, so clear all images.
            display_config.waiting_images.clear();
            if (display_config.displayed_image != nullptr) {
                {
                    fbl::AutoLock lock(controller_->mtx());
                    display_config.displayed_image->StartRetire();
                }
                display_config.displayed_image = nullptr;
                config_applied_ = false;
            }
        }
    }

    ApplyConfig();
}

void Client::HandleSetOwnership(const display_ControllerSetOwnershipRequest* req,
                                fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    // Only the virtcon can control ownership
    if (!is_vc_) {
        zxlogf(SPEW, "Ignoring non-virtcon ownership\n");
        return;
    }
    controller_->SetVcOwner(req->active);
}

void Client::HandleComputeLinearImageStride(
        const display_ControllerComputeLinearImageStrideRequest* req,
        fidl::Builder* resp_builder, const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<display_ControllerComputeLinearImageStrideResponse>();
    *resp_table = &display_ControllerComputeLinearImageStrideResponseTable;
    resp->stride = DC_IMPL_CALL(compute_linear_stride, req->width, req->pixel_format);
}

void Client::HandleAllocateVmo(const display_ControllerAllocateVmoRequest* req,
                               fidl::Builder* resp_builder,
                               zx_handle_t* handle_out, bool* has_handle_out,
                               const fidl_type_t** resp_table) {
    auto resp = resp_builder->New<display_ControllerAllocateVmoResponse>();
    *resp_table = &display_ControllerAllocateVmoResponseTable;

    resp->res = DC_IMPL_CALL(allocate_vmo, req->size, handle_out);
    *has_handle_out = resp->res == ZX_OK;
    resp->vmo = *has_handle_out ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;
}

bool Client::CheckConfig() {
    display_config_t* configs[configs_.size()];
    int idx = 0;
    for (auto& display_config : configs_) {
        if (display_config.has_pending_image) {
            if (display_config.pending_image != nullptr) {
                display_config.pending.image = display_config.pending_image->info();
                configs[idx++] = &display_config.pending;
            }
        } else if (!display_config.waiting_images.is_empty()) {
            display_config.pending.image = display_config.waiting_images.back().info();
            configs[idx++] = &display_config.pending;
        } else if (display_config.displayed_image != nullptr) {
            configs[idx++] = &display_config.current;
        }
    }

    return DC_IMPL_CALL(check_configuration, configs, idx);
}

void Client::ApplyConfig() {
    display_config_t* configs[configs_.size()];
    int idx = 0;
    for (auto& display_config : configs_) {
        // Find the newest image which is ready
        int32_t new_image = -1;
        if (!display_config.waiting_images.is_empty()) {
            for (auto iter = --display_config.waiting_images.cend(); iter.IsValid(); --iter) {
                if (iter->IsReady()) {
                    new_image = iter->id;
                    break;
                }
            }
        }
        if (new_image != -1) {
            if (display_config.displayed_image != nullptr) {
                fbl::AutoLock lock(controller_->mtx());
                display_config.displayed_image->StartRetire();
            }
            while (display_config.waiting_images.front().id != new_image) {
                auto early_retire = display_config.waiting_images.pop_front();
                early_retire->EarlyRetire();
            }
            display_config.displayed_image = display_config.waiting_images.pop_front();
            display_config.current.image = display_config.displayed_image->info();
            config_applied_ = false;
        }

        // Skip the display since there's nothing to show
        if (display_config.displayed_image == nullptr) {
            continue;
        }
        configs[idx++] = &display_config.current;
    }

    if (is_owner_ && !config_applied_) {
        config_applied_ = true;
        DisplayConfig* dc_configs[configs_.size()];
        int dc_idx = 0;
        for (auto& c : configs_) {
            dc_configs[dc_idx++] = &c;
        }
        controller_->OnConfigApplied(dc_configs, dc_idx);

        DC_IMPL_CALL(apply_configuration, configs, idx);
    }
}

void Client::NotifyDisplaysChanged(const int32_t* displays_added, uint32_t added_count,
                                   const int32_t* displays_removed, uint32_t removed_count) {
    ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(bytes, ZX_CHANNEL_MAX_MSG_BYTES);
    auto req = builder.New<display_ControllerCallbackOnDisplaysChangedRequest>();
    zx_status_t status;
    req->hdr.txid = next_txn_++;
    req->hdr.ordinal = display_ControllerCallbackOnDisplaysChangedOrdinal;
    req->added.count = added_count;
    req->added.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    req->removed.count = removed_count;
    req->removed.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

    if (added_count > 0) {
        auto coded_configs = builder.NewArray<display_Info>(added_count);
        for (unsigned i = 0; i < added_count; i++) {
            auto config = configs_.find(displays_added[i]);

            coded_configs[i].id = config->id;
            coded_configs[i].modes.count = 1;
            coded_configs[i].modes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
            coded_configs[i].pixel_format.count = config->pixel_format_count;
            coded_configs[i].pixel_format.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

            auto mode = builder.NewArray<display_Mode>(1);
            mode->horizontal_resolution = config->current.h_active;
            mode->vertical_resolution = config->current.v_active;
            uint64_t total_pxls = config->current.h_total * config->current.v_total;
            uint64_t pixel_clock_hz_e2 = config->current.pixel_clock_khz * 1000 * 100;
            mode->refresh_rate_e2 =
                    static_cast<uint32_t>((pixel_clock_hz_e2 + total_pxls - 1) / total_pxls);

            static_assert(sizeof(zx_pixel_format_t) == sizeof(int32_t), "Bad pixel format size");
            auto pixel_format = builder.NewArray<int32_t>(config->pixel_format_count);
            memcpy(pixel_format, config->pixel_formats.get(),
                   config->pixel_format_count * sizeof(int32_t));
        }
    }

    if (removed_count > 0) {
        auto removed_ids = builder.NewArray<int32_t>(removed_count);
        memcpy(removed_ids, displays_removed, sizeof(int32_t) * removed_count);
    }

    fidl::Message msg(builder.Finalize(), fidl::HandlePart());
    const char* err;
    ZX_DEBUG_ASSERT_MSG(
            msg.Validate(&display_ControllerCallbackOnDisplaysChangedRequestTable, &err) == ZX_OK,
            "Failed to validate \"%s\"", err);

    if ((status = msg.Write(callback_handle_.get(), 0)) != ZX_OK) {
        zxlogf(INFO, "Error writing remove message %d\n", status);
    }
}

void Client::SetOwnership(bool is_owner) {
    ZX_DEBUG_ASSERT(controller_->current_thread_is_loop());

    is_owner_ = is_owner;
    config_applied_ = false;

    if (callback_handle_) {
        display_ControllerCallbackOnClientOwnershipChangeRequest msg;
        msg.hdr.txid = next_txn_++;
        msg.hdr.ordinal = display_ControllerCallbackOnClientOwnershipChangeOrdinal;
        msg.has_ownership = is_owner;

        zx_status_t status = callback_handle_.write(0, &msg, sizeof(msg), nullptr, 0);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error writing remove message %d\n", status);
        }
    }

    ApplyConfig();

}

void Client::OnDisplaysChanged(fbl::unique_ptr<DisplayConfig>* displays_added,
                               uint32_t added_count,
                               int32_t* displays_removed, uint32_t removed_count) {
    for (unsigned i = 0; i < removed_count; i++) {
        configs_.erase(displays_removed[i]);
    }

    int32_t added_display_ids[added_count];
    for (unsigned i = 0; i < added_count; i++) {
        added_display_ids[i] = displays_added[0]->id;
        configs_.insert(fbl::move(displays_added[i]));
    }

    if (callback_handle_) {
        NotifyDisplaysChanged(added_display_ids, added_count, displays_removed, removed_count);
    }
}

zx_status_t Client::InitApiConnection(zx::handle* client_handle) {
    zx_status_t status;
    if ((status = zx_channel_create(0, server_handle_.reset_and_get_address(),
                                    client_handle->reset_and_get_address())) != ZX_OK) {
        zxlogf(ERROR, "Failed to create channels %d\n", status);
        return status;
    }

    api_wait_.set_object(server_handle_.get());
    api_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    if ((status = api_wait_.Begin(controller_->loop().async())) != ZX_OK) {
        // Clear the object, since that's used to detect whether or not api_wait_ is inited.
        api_wait_.set_object(ZX_HANDLE_INVALID);
        zxlogf(ERROR, "Failed to start waiting %d\n", status);
        return status;
    }

    mtx_init(&fence_mtx_, mtx_plain);

    return ZX_OK;
}

fbl::RefPtr<FenceReference> Client::GetFence(int32_t id) {
    if (id < 0) {
        return nullptr;
    }
    fbl::AutoLock lock(&fence_mtx_);
    auto fence = fences_.find(id);
    return fence.IsValid() ? fence->GetReference() : nullptr;
}

void Client::OnFenceFired(FenceReference* fence) {
    for (auto& display_config : configs_) {
        for (auto& waiting : display_config.waiting_images) {
            waiting.OnFenceReady(fence);
        }
    }
    ApplyConfig();
}

void Client::OnRefForFenceDead(Fence* fence) {
    fbl::AutoLock lock(&fence_mtx_);
    if (fence->OnRefDead()) {
        fences_.erase(fence->id);
    }
}

void Client::Reset() {
    ZX_DEBUG_ASSERT(controller_->loop().GetState() == ASYNC_LOOP_SHUTDOWN
            || controller_->current_thread_is_loop());
    pending_config_valid_ = false;

    if (api_wait_.object() != ZX_HANDLE_INVALID) {
        api_wait_.Cancel();
        api_wait_.set_object(ZX_HANDLE_INVALID);
    }
    server_handle_.reset();
    callback_handle_.reset();

    // Use a temporary list to prevent double locking when resetting
    fbl::SinglyLinkedList<fbl::RefPtr<Fence>> fences;
    {
        fbl::AutoLock lock(&fence_mtx_);
        while (!fences_.is_empty()) {
            fences.push_front(fences_.erase(fences_.begin()));
        }
    }
    while (!fences.is_empty()) {
        fences.pop_front()->Reset();
    }

    for (auto& config : configs_) {
        if (config.pending_image) {
            config.pending_image->DiscardAcquire();
            config.pending_image = nullptr;
            config.has_pending_image = false;
        }
        for (auto& waiting : config.waiting_images) {
            waiting.EarlyRetire();
        }
        config.waiting_images.clear();

        if (config.displayed_image) {
            {
                fbl::AutoLock lock(controller_->mtx());
                config.displayed_image->StartRetire();
            }
            config.displayed_image = nullptr;
            config_applied_ = false;
        }
    }

    ApplyConfig();

    images_.clear();

    proxy_->OnClientDead();
}

Client::Client(Controller* controller, ClientProxy* proxy, bool is_vc)
        : controller_(controller), proxy_(proxy), is_vc_(is_vc) { }

Client::~Client() {
    ZX_DEBUG_ASSERT(server_handle_ == ZX_HANDLE_INVALID);
}

void ClientProxy::SetOwnership(bool is_owner) {
    auto task = new async::Task();
    task->set_handler([client_handler = &handler_, is_owner]
                       (async_t* async, async::Task* task, zx_status_t status) {
            if (status == ZX_OK) {
                client_handler->SetOwnership(is_owner);
            }

            delete task;

    });
    task->Post(controller_->loop().async());
}

zx_status_t ClientProxy::OnDisplaysChanged(const DisplayInfo** displays_added,
                                           uint32_t added_count, const int32_t* displays_removed,
                                           uint32_t removed_count) {
    fbl::unique_ptr<fbl::unique_ptr<DisplayConfig>[]> added;
    fbl::unique_ptr<int32_t[]> removed;

    fbl::AllocChecker ac;
    if (added_count) {
        added = fbl::unique_ptr<fbl::unique_ptr<DisplayConfig>[]>(
                new (&ac) fbl::unique_ptr<DisplayConfig>[added_count]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }
    if (removed_count) {
        removed = fbl::unique_ptr<int32_t[]>(new (&ac) int32_t[removed_count]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    memcpy(removed.get(), displays_removed, sizeof(int32_t) * removed_count);

    uint32_t added_idx;
    for (added_idx = 0; added_idx < added_count; added_idx++) {
        auto d = displays_added[added_idx];
        auto config = fbl::make_unique_checked<DisplayConfig>(&ac);
        if (!ac.check()) {
            zxlogf(INFO, "Out of memory when processing hotplug\n");
            break;
        }

        auto pixel_formats = fbl::unique_ptr<zx_pixel_format_t[]>(
                new (&ac) zx_pixel_format_t[d->info.pixel_format_count]);
        if (!ac.check()) {
            zxlogf(INFO, "Out of memory when processing hotplug\n");
            break;
        }

        config->id = d->id;
        config->pixel_format_count = d->info.pixel_format_count;
        config->pixel_formats = fbl::move(pixel_formats);
        memcpy(config->pixel_formats.get(), d->info.pixel_formats,
               sizeof(zx_pixel_format_t) * d->info.pixel_format_count);

        config->current.display_id = d->id;
        config->current.pixel_clock_khz = d->preferred_timing.pixel_freq_10khz * 10;
        config->current.h_active = d->preferred_timing.horizontal_addressable;
        config->current.h_sync_start =
                d->preferred_timing.horizontal_front_porch + config->current.h_active;
        config->current.h_sync_end =
                d->preferred_timing.horizontal_sync_pulse + config->current.h_sync_start;
        config->current.h_total =
                d->preferred_timing.horizontal_back_porch + config->current.h_sync_end;
        config->current.v_active = d->preferred_timing.vertical_addressable;
        config->current.v_sync_start =
                d->preferred_timing.vertical_front_porch + config->current.v_active;
        config->current.v_sync_end =
                d->preferred_timing.vertical_sync_pulse + config->current.v_sync_start;
        config->current.v_total =
                d->preferred_timing.vertical_back_porch + config->current.v_sync_end;
        config->current.pixel_clock_khz = d->preferred_timing.pixel_freq_10khz * 10;
        config->current.mode_flags =
                (d->preferred_timing.vertical_sync_polarity ? MODE_FLAG_VSYNC_POSITIVE : 0)
                | (d->preferred_timing.horizontal_sync_polarity ? MODE_FLAG_HSYNC_POSITIVE : 0);
        config->pending = config->current;

        added[added_idx] = fbl::move(config);
    }

    auto task = new async::Task();
    task->set_handler([client_handler = &handler_,
                       added_ptr = added.release(), removed_ptr = removed.release(),
                       added_idx, removed_count]
                       (async_t* async, async::Task* task, zx_status_t status) {
            if (status == ZX_OK) {
                client_handler->OnDisplaysChanged(added_ptr, added_idx,
                                                  removed_ptr, removed_count);
            }

            delete[] added_ptr;
            delete[] removed_ptr;
            delete task;
    });
    return task->Post(controller_->loop().async());
}

void ClientProxy::OnClientDead() {
    fbl::AutoLock lock(&bind_lock_);
    bound_ = false;
}

void ClientProxy::Close() {
    {
        fbl::AutoLock lock(&bind_lock_);
        closed_ = true;

        if (!bound_) {
            return;
        }
    }

    if (controller_->current_thread_is_loop()) {
        handler_.Reset();
    } else {
        mtx_t mtx;
        mtx_init(&mtx, mtx_plain);
        cnd_t cnd;
        cnd_init(&cnd);
        bool done = false;

        mtx_lock(&mtx);
        auto task = new async::Task();
        task->set_handler([client_handler = &handler_,
                           cnd_ptr = &cnd, mtx_ptr = &mtx, done_ptr = &done]
                           (async_t* async, async::Task* task, zx_status_t status) {
                mtx_lock(mtx_ptr);

                client_handler->Reset();

                *done_ptr = true;
                cnd_signal(cnd_ptr);
                mtx_unlock(mtx_ptr);

                delete task;
        });
        if (task->Post(controller_->loop().async()) != ZX_OK) {
            // Tasks only fail to post if the looper is dead. That shouldn't actually
            // happen, but if it does then it's safe to call Reset on this thread anyway.
            delete task;
            handler_.Reset();
        } else {
            while (!done) {
                cnd_wait(&cnd, &mtx);
            }
        }
        mtx_unlock(&mtx);
    }
}

zx_status_t ClientProxy::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* actual) {
    switch (op) {
    case IOCTL_DISPLAY_CONTROLLER_GET_HANDLE: {
        if (out_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        fbl::AutoLock lock(&bind_lock_);
        if (closed_) {
            return ZX_ERR_PEER_CLOSED;
        }

        if (bound_) {
            return ZX_ERR_ALREADY_BOUND;
        }

        zx::handle client_handle;
        zx_status_t status = handler_.InitApiConnection(&client_handle);
        if (status != ZX_OK) {
            return status;
        }

        bound_ = true;
        *reinterpret_cast<zx_handle_t*>(out_buf) = client_handle.release();
        *actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t ClientProxy::DdkClose(uint32_t flags) {
    controller_->OnClientClosed(this);
    Close();
    return ZX_OK;
}

void ClientProxy::DdkRelease() {
    delete this;
}

ClientProxy::ClientProxy(Controller* controller, bool is_vc)
        : ClientParent(controller->zxdev()),
          controller_(controller), is_vc_(is_vc), handler_(controller_, this, is_vc_) {
    mtx_init(&bind_lock_, mtx_plain);
}

ClientProxy::~ClientProxy() {
    ZX_DEBUG_ASSERT(!bound_);
}

} // namespace display

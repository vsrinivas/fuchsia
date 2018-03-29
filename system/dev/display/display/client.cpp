// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
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
    const fidl_type_t* out_type = nullptr;

    switch (msg.ordinal()) {
    HANDLE_REQUEST_CASE(SetControllerCallback);
    default:
        zxlogf(INFO, "Unknown ordinal %d\n", msg.ordinal());
    }

    fidl::BytePart resp_bytes = builder.Finalize();
    if (resp_bytes.actual() != 0) {
        ZX_DEBUG_ASSERT(out_type != nullptr);

        fidl::Message resp(fbl::move(resp_bytes), fidl::HandlePart());
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
        zxlogf(ERROR, "Failed to start waiting %d\n", status);
        return status;
    }

    return ZX_OK;
}

void Client::Reset() {
    ZX_DEBUG_ASSERT(controller_->loop().GetState() == ASYNC_LOOP_SHUTDOWN
            || controller_->current_thread_is_loop());

    api_wait_.Cancel();
    server_handle_.reset();
    callback_handle_.reset();

    proxy_->OnClientDead();
}

Client::Client(Controller* controller, ClientProxy* proxy)
        : controller_(controller), proxy_(proxy) { }

Client::~Client() {
    ZX_DEBUG_ASSERT(server_handle_ == ZX_HANDLE_INVALID);
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

ClientProxy::ClientProxy(Controller* controller)
        : ClientParent(controller->zxdev()),
          controller_(controller), handler_(controller_, this) {
    mtx_init(&bind_lock_, mtx_plain);
}

ClientProxy::~ClientProxy() {
    ZX_DEBUG_ASSERT(!bound_);
}

} // namespace display

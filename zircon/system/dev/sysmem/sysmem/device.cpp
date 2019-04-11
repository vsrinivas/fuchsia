// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include "allocator.h"
#include "amlogic_memory_allocator.h"
#include "buffer_collection_token.h"
#include "macros.h"

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/device/sysmem.h>

namespace {

fuchsia_sysmem_DriverConnector_ops_t driver_connector_ops = {
    .Connect = fidl::Binder<Device>::BindMember<&Device::Connect>,
    .GetProtectedMemoryInfo = fidl::Binder<Device>::BindMember<&Device::GetProtectedMemoryInfo>,
};

zx_status_t sysmem_message(void* device_ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_sysmem_DriverConnector_dispatch(device_ctx, txn, msg,
                                                   &driver_connector_ops);
}

// -Werror=missing-field-initializers seems more paranoid than I want here.
zx_protocol_device_t sysmem_device_ops = [] {
    zx_protocol_device_t tmp{};
    tmp.version = DEVICE_OPS_VERSION;
    tmp.message = sysmem_message;
    return tmp;
}();

zx_protocol_device_t out_of_proc_sysmem_protocol_ops = [] {
    zx_protocol_device_t tmp{};
    tmp.version = DEVICE_OPS_VERSION;
    // tmp.message is not used - sysmem_device_ops.message is used for incoming
    // FIDL messages.
    return tmp;
}();

zx_status_t in_proc_sysmem_Connect(void* ctx,
                                   zx_handle_t allocator2_request_param) {
    Device* self = static_cast<Device*>(ctx);
    return self->Connect(allocator2_request_param);
}

// In-proc sysmem interface.  Essentially an in-proc version of
// fuchsia.sysmem.DriverConnector.
sysmem_protocol_ops_t in_proc_sysmem_protocol_ops = {
    .connect = in_proc_sysmem_Connect,
};

} // namespace

Device::Device(zx_device_t* parent_device, Driver* parent_driver)
    : parent_device_(parent_device),
      parent_driver_(parent_driver), in_proc_sysmem_protocol_{
                                         .ops = &in_proc_sysmem_protocol_ops,
                                         .ctx = this} {
    ZX_DEBUG_ASSERT(parent_device_);
    ZX_DEBUG_ASSERT(parent_driver_);
}

zx_status_t Device::Bind() {
    zx_status_t status =
        device_get_protocol(parent_device_, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        DRIVER_ERROR(
            "Failed device_get_protocol() ZX_PROTOCOL_PDEV - status: %d",
            status);
        return status;
    }

    uint64_t protected_memory_size = 0;

    sysmem_metadata_t metadata;

    size_t metadata_actual;
    status = device_get_metadata(parent_device_, SYSMEM_METADATA, &metadata, sizeof(metadata), &metadata_actual);
    if (status == ZX_OK && metadata_actual == sizeof(metadata)) {
        pdev_device_info_vid_ = metadata.vid;
        pdev_device_info_pid_ = metadata.pid;
        protected_memory_size = metadata.protected_memory_size;
    }

    status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed pdev_get_bti() - status: %d", status);
        return status;
    }

    zx::bti bti_copy;
    status = bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, &bti_copy);
    if (status != ZX_OK) {
        DRIVER_ERROR("BTI duplicate failed: %d", status);
        return status;
    }

    // TODO: Separate protected memory allocator into separate driver or library
    if (pdev_device_info_vid_ == PDEV_VID_AMLOGIC && protected_memory_size > 0) {
        auto amlogic_allocator = std::make_unique<AmlogicMemoryAllocator>(std::move(bti_copy));
        status = amlogic_allocator->Init(protected_memory_size);
        if (status != ZX_OK) {
            DRIVER_ERROR("Failed to init allocator for amlogic protected memory: %d", status);
            return status;
        }
        protected_allocator_ = std::move(amlogic_allocator);
    }

    pbus_protocol_t pbus;
    status = device_get_protocol(parent_device_, ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        DRIVER_ERROR("ZX_PROTOCOL_PBUS not available %d \n", status);
        return status;
    }

    device_add_args_t device_add_args = {};
    device_add_args.version = DEVICE_ADD_ARGS_VERSION;
    device_add_args.name = "sysmem";
    device_add_args.ctx = this;
    device_add_args.ops = &sysmem_device_ops;

    // ZX_PROTOCOL_SYSMEM causes /dev/class/sysmem to get created, and flags
    // support for the fuchsia.sysmem.DriverConnector protocol.  The .message
    // callback used is sysmem_device_ops.message, not
    // sysmem_protocol_ops.message.
    device_add_args.proto_id = ZX_PROTOCOL_SYSMEM;
    device_add_args.proto_ops = &out_of_proc_sysmem_protocol_ops;
    device_add_args.flags = DEVICE_ADD_INVISIBLE;

    status = device_add(parent_device_, &device_add_args, &device_);
    if (status != ZX_OK) {
        DRIVER_ERROR("Failed to bind device");
        return status;
    }

    // Register the sysmem protocol with the platform bus.
    //
    // This is essentially the in-proc version of
    // fuchsia.sysmem.DriverConnector.
    //
    // We should only pbus_register_protocol() if device_add() succeeded, but if
    // pbus_register_protocol() fails, we should remove the device without it
    // ever being visible.
    status = pbus_register_protocol(
        &pbus, ZX_PROTOCOL_SYSMEM, &in_proc_sysmem_protocol_,
        sizeof(in_proc_sysmem_protocol_));
    if (status != ZX_OK) {
        zx_status_t remove_status = device_remove(device_);
        // If this failed, we're potentially leaving the device invisible in a
        // --release build, which is about the best we can do if removing fails.
        // Of course, remove shouldn't fail in the first place.
        ZX_DEBUG_ASSERT(remove_status == ZX_OK);
        return status;
    }

    // We only do this if Bind() fully worked.  Else we don't want any client
    // to be able to see the device.  This call returns void, thankfully.
    device_make_visible(device_);

    return ZX_OK;
}

zx_status_t Device::Connect(zx_handle_t allocator_request) {
    zx::channel local_allocator_request(allocator_request);
    // The Allocator is channel-owned / self-owned.
    Allocator::CreateChannelOwned(std::move(local_allocator_request), this);
    return ZX_OK;
}

zx_status_t Device::GetProtectedMemoryInfo(fidl_txn* txn) {
    if (!protected_allocator_) {
        return fuchsia_sysmem_DriverConnectorGetProtectedMemoryInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, 0u, 0u);
    }

    uint64_t base;
    uint64_t size;
    zx_status_t status = protected_allocator_->GetProtectedMemoryInfo(&base, &size);
    return fuchsia_sysmem_DriverConnectorGetProtectedMemoryInfo_reply(txn, status, base, size);
}

const zx::bti& Device::bti() {
    return bti_;
}

uint32_t Device::pdev_device_info_vid() {
    ZX_DEBUG_ASSERT(pdev_device_info_vid_ !=
                    std::numeric_limits<uint32_t>::max());
    return pdev_device_info_vid_;
}

uint32_t Device::pdev_device_info_pid() {
    ZX_DEBUG_ASSERT(pdev_device_info_pid_ !=
                    std::numeric_limits<uint32_t>::max());
    return pdev_device_info_pid_;
}

void Device::TrackToken(BufferCollectionToken* token) {
    zx_koid_t server_koid = token->server_koid();
    ZX_DEBUG_ASSERT(server_koid != ZX_KOID_INVALID);
    ZX_DEBUG_ASSERT(tokens_by_koid_.find(server_koid) == tokens_by_koid_.end());
    tokens_by_koid_.insert({server_koid, token});
}

void Device::UntrackToken(BufferCollectionToken* token) {
    zx_koid_t server_koid = token->server_koid();
    if (server_koid == ZX_KOID_INVALID) {
        // The caller is allowed to un-track a token that never saw
        // SetServerKoid().
        return;
    }
    auto iter = tokens_by_koid_.find(server_koid);
    ZX_DEBUG_ASSERT(iter != tokens_by_koid_.end());
    tokens_by_koid_.erase(iter);
}

BufferCollectionToken* Device::FindTokenByServerChannelKoid(
    zx_koid_t token_server_koid) {
    auto iter = tokens_by_koid_.find(token_server_koid);
    if (iter == tokens_by_koid_.end()) {
        return nullptr;
    }
    return iter->second;
}

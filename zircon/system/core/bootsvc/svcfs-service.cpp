// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svcfs-service.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/job.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include "util.h"

namespace {

struct ArgumentsData {
    zx::vmo vmo;
    size_t size;
};

zx_status_t ArgumentsGet(void* ctx, fidl_txn_t* txn) {
    auto data = static_cast<const ArgumentsData*>(ctx);
    zx::vmo dup;
    zx_status_t status = data->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
        printf("bootsvc: Failed to duplicate boot arguments VMO: %s\n",
               zx_status_get_string(status));
        return status;
    }
    return fuchsia_boot_ArgumentsGet_reply(txn, dup.release(), data->size);
}

constexpr fuchsia_boot_Arguments_ops kArgumentsOps = {
    .Get = ArgumentsGet,
};

struct ItemsData {
    zx::vmo vmo;
    bootsvc::ItemMap map;
};

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
    auto data = static_cast<const ItemsData*>(ctx);
    auto it = data->map.find(bootsvc::ItemKey{type, extra});
    if (it == data->map.end()) {
        return fuchsia_boot_ItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
    }
    auto& item = it->second;
    auto buf = std::make_unique<uint8_t[]>(item.length);
    zx_status_t status = data->vmo.read(buf.get(), item.offset, item.length);
    if (status != ZX_OK) {
        printf("bootsvc: Failed to read from boot image VMO: %s\n", zx_status_get_string(status));
        return status;
    }
    zx::vmo payload;
    status = zx::vmo::create(item.length, 0, &payload);
    if (status != ZX_OK) {
        printf("bootsvc: Failed to create payload VMO: %s\n", zx_status_get_string(status));
        return status;
    }
    status = payload.write(buf.get(), 0, item.length);
    if (status != ZX_OK) {
        printf("bootsvc: Failed to write to payload VMO: %s\n", zx_status_get_string(status));
        return status;
    }
    return fuchsia_boot_ItemsGet_reply(txn, payload.release(), item.length);
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
};

zx_status_t RootResourceGet(void* ctx, fidl_txn_t* txn) {
    zx::resource resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
    if (!resource.is_valid()) {
        printf("bootsvc: Invalid root resource\n");
        return ZX_ERR_NOT_FOUND;
    }
    return fuchsia_boot_RootResourceGet_reply(txn, resource.release());
}

zx_status_t RootJobGet(void* ctx, fidl_txn_t* txn) {
    zx::job dup;
    zx_status_t status = zx::job::default_job()->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
        printf("bootsvc: Failed to duplicate root job: %s\n", zx_status_get_string(status));
        return status;
    }
    return fuchsia_boot_RootJobGet_reply(txn, dup.release());
}

constexpr fuchsia_boot_RootJob_ops kRootJobOps = {
    .Get = RootJobGet,
};

constexpr fuchsia_boot_RootResource_ops kRootResourceOps = {
    .Get = RootResourceGet,
};

} // namespace

namespace bootsvc {

fbl::RefPtr<SvcfsService> SvcfsService::Create(async_dispatcher_t* dispatcher) {
    return fbl::AdoptRef(new SvcfsService(dispatcher));
}

SvcfsService::SvcfsService(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher), root_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void SvcfsService::AddService(const char* service_name, fbl::RefPtr<fs::Service> service) {
    root_->AddEntry(service_name, std::move(service));
}

zx_status_t SvcfsService::CreateRootConnection(zx::channel* out) {
    return CreateVnodeConnection(&vfs_, root_, out);
}

fbl::RefPtr<fs::Service> CreateArgumentsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                                uint64_t size) {
    ArgumentsData data{std::move(vmo), size};
    return fbl::MakeRefCounted<fs::Service>(
        [dispatcher, data = std::move(data)](zx::channel channel) mutable {
            auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Arguments_dispatch);
            return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kArgumentsOps);
        });
}

fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map) {
    ItemsData data{std::move(vmo), std::move(map)};
    return fbl::MakeRefCounted<fs::Service>(
        [dispatcher, data = std::move(data)](zx::channel channel) mutable {
            auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
            return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kItemsOps);
        });
}

fbl::RefPtr<fs::Service> CreateRootJobService(async_dispatcher_t* dispatcher) {
    return fbl::MakeRefCounted<fs::Service>([dispatcher](zx::channel channel) {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootJob_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, nullptr, &kRootJobOps);
    });
}

fbl::RefPtr<fs::Service> CreateRootResourceService(async_dispatcher_t* dispatcher) {
    return fbl::MakeRefCounted<fs::Service>([dispatcher](zx::channel channel) {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootResource_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, nullptr, &kRootResourceOps);
    });
}

} // namespace bootsvc

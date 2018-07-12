// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sysmem/sysmem.h>

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl/bind.h>
#include <string.h>
#include <zircon/syscalls.h>

static zx_status_t Allocator_AllocateCollection(void* ctx,
                                                uint32_t buffer_count,
                                                const fuchsia_sysmem_BufferSpec* spec,
                                                const fuchsia_sysmem_BufferUsage* usage,
                                                fidl_txn_t* txn) {
    fuchsia_sysmem_BufferCollectionInfo info;
    memset(&info, 0, sizeof(info));
    return fuchsia_sysmem_AllocatorAllocateCollection_reply(txn, ZX_ERR_NOT_SUPPORTED, &info);
}

static zx_status_t Allocator_AllocateSharedCollection(void* ctx,
                                                      uint32_t buffer_count,
                                                      const fuchsia_sysmem_BufferSpec* spec,
                                                      zx_handle_t token_peer,
                                                      fidl_txn_t* txn) {
    return fuchsia_sysmem_AllocatorAllocateSharedCollection_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t Allocator_BindSharedCollection(void* ctx,
                                                  const fuchsia_sysmem_BufferUsage* usage,
                                                  zx_handle_t token,
                                                  fidl_txn_t* txn) {
    fuchsia_sysmem_BufferCollectionInfo info;
    memset(&info, 0, sizeof(info));
    return fuchsia_sysmem_AllocatorBindSharedCollection_reply(txn, ZX_ERR_NOT_SUPPORTED, &info);
}

static constexpr const fuchsia_sysmem_Allocator_ops_t allocator_ops = {
    .AllocateCollection = Allocator_AllocateCollection,
    .AllocateSharedCollection = Allocator_AllocateSharedCollection,
    .BindSharedCollection = Allocator_BindSharedCollection,
};

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher,
                           const char* service_name, zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_sysmem_Allocator_Name)) {
        return fidl_bind(dispatcher, request,
                         (fidl_dispatch_t*)fuchsia_sysmem_Allocator_dispatch,
                         ctx, &allocator_ops);
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* sysmem_services[] = {
    fuchsia_sysmem_Allocator_Name,
    nullptr,
};

static constexpr zx_service_ops_t sysmem_ops = {
    .init = nullptr,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t sysmem_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = sysmem_services,
    .ops = &sysmem_ops,
};

const zx_service_provider_t* sysmem_get_service_provider() {
    return &sysmem_service_provider;
}

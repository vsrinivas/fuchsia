// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/kernel-mexec/kernel-mexec.h>

#include <ddk/device.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/kernel/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/bind.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <libzbi/zbi-cpp.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/limits.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

const internal::MexecSysCalls kMexecSysCalls {
    .mexec = zx_system_mexec,
    .mexec_payload_get = zx_system_mexec_payload_get,
};

zx_status_t PerformMexec(void* ctx_raw, zx_handle_t raw_kernel, zx_handle_t raw_bootdata) {
    return PerformMexec(ctx_raw, raw_kernel, raw_bootdata, kMexecSysCalls);
}

constexpr const fuchsia_kernel_MexecBroker_ops_t kInterfaceOps = {
    .PerformMexec = PerformMexec,
};

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher,
                    const char* service_name, zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_kernel_MexecBroker_Name)) {
        return fidl_bind(dispatcher, request,
                         (fidl_dispatch_t*)fuchsia_kernel_MexecBroker_dispatch,
                         ctx, &kInterfaceOps);
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

constexpr const char* kServices[] = {
    fuchsia_kernel_MexecBroker_Name,
    nullptr,
};

constexpr zx_service_ops_t kServiceOps = {
    .init = nullptr,
    .connect = Connect,
    .release = nullptr,
};

constexpr zx_service_provider_t kServiceProvider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kServices,
    .ops = &kServiceOps,
};

zx_status_t Suspend(const zx::unowned_channel& services_directory, int suspend_flag) {
    zx::channel channel, channel_remote;
    zx_status_t status = zx::channel::create(0, &channel, &channel_remote);
    if (status != ZX_OK) {
        fprintf(stderr, "failed to create channel: %d\n", status);
        return status;
    }

    const char* service = fuchsia_device_manager_Administrator_Name;
    status = fdio_service_connect_at(services_directory->get(),
                                     service, channel_remote.release());
    if (status != ZX_OK) {
        fprintf(stderr, "failed to connect to service: %d\n", status);
        return status;
    }

    zx_status_t call_status = ZX_OK;
    status = fuchsia_device_manager_AdministratorSuspend(channel.get(), suspend_flag,
                                                         &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        fprintf(stderr, "Failed to call suspend: local:%d remote:%d\n", status, call_status);
        return status == ZX_OK ? call_status : status;
    }

    return ZX_OK;
}

} // namespace

namespace internal {

zx_status_t PerformMexec(void* ctx_raw, zx_handle_t raw_kernel, zx_handle_t raw_bootdata,
                               MexecSysCalls sys_calls) {
    const KernelMexecContext* context = reinterpret_cast<KernelMexecContext*>(ctx_raw);

    zx_status_t status;
    constexpr size_t kBootdataExtraSz = ZX_PAGE_SIZE * 4;

    zx::vmo kernel(raw_kernel);
    zx::vmo original_bootdata(raw_bootdata);
    zx::vmo bootdata;

    fzl::OwnedVmoMapper mapper;

    uint8_t* buffer = new uint8_t[kBootdataExtraSz];
    memset(buffer, 0, kBootdataExtraSz);
    fbl::unique_ptr<uint8_t[]> deleter;
    deleter.reset(buffer);

    size_t original_size;
    status = original_bootdata.get_size(&original_size);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: could not get bootdata vmo size, st = %d\n", status);
        return status;
    }

    status = original_bootdata.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0,
                                            original_size + ZX_PAGE_SIZE * 4, &bootdata);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: failed to clone bootdata st = %d\n", status);
        return status;
    }

    size_t vmo_size;
    status = bootdata.get_size(&vmo_size);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: failed to get new bootdata size, st = %d\n", status);
        return status;
    }

    status = sys_calls.mexec_payload_get(context->root_resource, buffer, kBootdataExtraSz);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: mexec get payload returned %d\n", status);
        return status;
    }

    zx::vmo mapped_bootdata;
    status = bootdata.duplicate(ZX_RIGHT_SAME_RIGHTS, &mapped_bootdata);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: failed to duplicate bootdata handle, st = %d\n", status);
        return status;
    }

    if (!mapped_bootdata.is_valid()) {
        fprintf(stderr, "kernel-mexec: mapped bootdata not valid!\n");
    }
    status = mapper.Map(std::move(mapped_bootdata));
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: failed to map bootdata vmo, status = %d\n", status);
        return status;
    }

    void* bootdata_ptr = mapper.start();
    zbi::Zbi bootdata_zbi(static_cast<uint8_t*>(bootdata_ptr), vmo_size);
    zbi::Zbi mexec_payload_zbi(buffer);

    zbi_result_t zbi_status = bootdata_zbi.Extend(mexec_payload_zbi);
    if (zbi_status != ZBI_RESULT_OK) {
        fprintf(stderr, "kernel-mexec: failed to extend bootdata zbi, st = %d\n", zbi_status);
        return ZX_ERR_INTERNAL;
    }

    status = Suspend(context->devmgr_channel, DEVICE_SUSPEND_FLAG_MEXEC);
    if (status != ZX_OK) {
        fprintf(stderr, "kernel-mexec: failed to suspend device, st = %d\n", status);
        return status;
    }

    sys_calls.mexec(context->root_resource, kernel.release(), bootdata.release());

    // We should never get here, we should be off running the new kernel and new
    // system.
    fprintf(stderr, "kernel-mexec: mexec system call returned!\n");

    return ZX_ERR_BAD_STATE;
}

} // namespace internal

const zx_service_provider_t* kernel_mexec_get_service_provider() {
    return &kServiceProvider;
}

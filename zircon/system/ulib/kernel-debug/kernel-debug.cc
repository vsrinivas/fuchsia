// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/kernel-debug/kernel-debug.h>

#include <fuchsia/kernel/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/zircon-internal/ktrace.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace {

zx_status_t HandleSendDebugCommand(void* ctx, const char* command, size_t command_size, fidl_txn_t* txn) {
    const zx_handle_t root_resource = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));
    const auto status = zx_debug_send_command(root_resource, command, command_size);
    return fuchsia_kernel_DebugBrokerSendDebugCommand_reply(txn, status);
}

zx_status_t HandleSetTracingEnabled(void* ctx, bool enabled, fidl_txn_t* txn) {
    const zx_handle_t root_resource = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));
    zx_status_t status;
    if (enabled) {
        status = zx_ktrace_control(root_resource, KTRACE_ACTION_START, KTRACE_GRP_ALL, nullptr);
    } else {
        status = zx_ktrace_control(root_resource, KTRACE_ACTION_STOP, 0, nullptr);
        if (status == ZX_OK) {
            status = zx_ktrace_control(root_resource, KTRACE_ACTION_REWIND, 0, nullptr);
        }
    }
    return fuchsia_kernel_DebugBrokerSendDebugCommand_reply(txn, status);
}

constexpr const fuchsia_kernel_DebugBroker_ops_t kInterfaceOps = {
    .SendDebugCommand = HandleSendDebugCommand,
    .SetTracingEnabled = HandleSetTracingEnabled,
};

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher,
                           const char* service_name, zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_kernel_DebugBroker_Name)) {
        return fidl_bind(dispatcher, request,
                         (fidl_dispatch_t*)fuchsia_kernel_DebugBroker_dispatch,
                         ctx, &kInterfaceOps);
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

constexpr const char* kServices[] = {
    fuchsia_kernel_DebugBroker_Name,
    nullptr,
};

constexpr zx_service_ops_t kServiceOps = {
    .init = nullptr,
    .connect = Connect,
    .release = nullptr,
};

constexpr zx_service_provider_t kDebugBrokerServiceProvider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kServices,
    .ops = &kServiceOps,
};

} // namespace

const zx_service_provider_t* kernel_debug_get_service_provider() {
    return &kDebugBrokerServiceProvider;
}

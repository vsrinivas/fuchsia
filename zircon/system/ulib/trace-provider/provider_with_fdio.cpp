// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(PT-63): This file contains wrappers that use fdio to connect to
// trace manager. An open question is whether to keep them.

#include <stdio.h>

#include <lib/zx/process.h>
#include <trace-provider/fdio_connect.h>
#include <trace-provider/provider.h>

#include <zircon/assert.h>
#include <zircon/status.h>

trace_provider_t* trace_provider_create_with_name_fdio(
        async_dispatcher_t* dispatcher, const char* name) {
    ZX_DEBUG_ASSERT(dispatcher);

    zx_handle_t to_service;
    auto status = trace_provider_connect_with_fdio(&to_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: connection failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    return trace_provider_create_with_name_etc(to_service, dispatcher, name);
}

trace_provider_t* trace_provider_create_with_fdio(
        async_dispatcher_t* dispatcher) {
    ZX_DEBUG_ASSERT(dispatcher);

    auto self = zx::process::self();
    char name[ZX_MAX_NAME_LEN];
    auto status = self->get_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: error getting process name: status=%d(%s)\n",
                status, zx_status_get_string(status));
        name[0] = '\0';
    }

    return trace_provider_create_with_name_fdio(dispatcher, name);
}

trace_provider_t* trace_provider_create_synchronously_with_fdio(
        async_dispatcher_t* dispatcher, const char* name,
        bool* out_manager_is_tracing_already) {
    ZX_DEBUG_ASSERT(dispatcher);

    zx_handle_t to_service;
    auto status = trace_provider_connect_with_fdio(&to_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: connection failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    return trace_provider_create_synchronously_etc(
        to_service, dispatcher, name, out_manager_is_tracing_already);
}

trace_provider_t* trace_provider_create_with_name(
        async_dispatcher_t* dispatcher, const char* name) {
    return trace_provider_create_with_name_fdio(dispatcher, name);
}

trace_provider_t* trace_provider_create(async_dispatcher_t* dispatcher) {
    return trace_provider_create_with_fdio(dispatcher);
}

trace_provider_t* trace_provider_create_synchronously(
        async_dispatcher_t* dispatcher, const char* name,
        bool* out_manager_is_tracing_already) {
    return trace_provider_create_synchronously_with_fdio(
        dispatcher, name, out_manager_is_tracing_already);
}

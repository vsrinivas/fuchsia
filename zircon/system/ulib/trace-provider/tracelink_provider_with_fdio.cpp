// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** PT-127 ****************************************************************
// This file is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

#include <stdio.h>

#include <lib/zx/process.h>
#include <trace-provider/fdio_connect.h>
#include <trace-provider/tracelink_provider.h>

#include <zircon/assert.h>
#include <zircon/status.h>

tracelink_provider_t* tracelink_provider_create_with_name_fdio(
        async_dispatcher_t* dispatcher, const char* name) {
    ZX_DEBUG_ASSERT(dispatcher);

    zx_handle_t to_service;
    auto status = tracelink_provider_connect_with_fdio(&to_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: connection failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    return tracelink_provider_create_with_name_etc(to_service, dispatcher, name);
}

tracelink_provider_t* tracelink_provider_create_synchronously_with_fdio(
        async_dispatcher_t* dispatcher, const char* name,
        bool* out_manager_is_tracing_already) {
    ZX_DEBUG_ASSERT(dispatcher);

    zx_handle_t to_service;
    auto status = tracelink_provider_connect_with_fdio(&to_service);
    if (status != ZX_OK) {
        fprintf(stderr, "TraceProvider: connection failed: status=%d(%s)\n",
                status, zx_status_get_string(status));
        return nullptr;
    }

    return tracelink_provider_create_synchronously_etc(
        to_service, dispatcher, name, out_manager_is_tracing_already);
}

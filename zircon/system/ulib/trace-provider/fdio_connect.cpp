// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A helper library for connecting to the trace manager via fdio.

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <trace-provider/fdio_connect.h>

const char kServicePath[] = "/svc/fuchsia.tracing.provider.Registry";
const char kTracelinkServicePath[] = "/svc/fuchsia.tracelink.Registry";

zx_status_t trace_provider_connect_with_fdio(zx_handle_t* out_client) {
    // Connect to the trace registry.
    zx::channel registry_client;
    zx::channel registry_service;
    zx_status_t status = zx::channel::create(0u, &registry_client, &registry_service);
    if (status != ZX_OK)
        return status;

    status = fdio_service_connect(kServicePath,
                                  registry_service.release()); // takes ownership
    if (status != ZX_OK)
        return status;

    *out_client = registry_client.release();
    return ZX_OK;
}

// *** PT-127 ****************************************************************
// This function is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

zx_status_t tracelink_provider_connect_with_fdio(zx_handle_t* out_client) {
    // Connect to the trace registry.
    zx::channel registry_client;
    zx::channel registry_service;
    zx_status_t status = zx::channel::create(0u, &registry_client, &registry_service);
    if (status != ZX_OK)
        return status;

    status = fdio_service_connect(kTracelinkServicePath,
                                  registry_service.release()); // takes ownership
    if (status != ZX_OK)
        return status;

    *out_client = registry_client.release();
    return ZX_OK;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// echo -n "zx_service_provider_v0.1" | sha256sum | cut -c1-16
#define SERVICE_PROVIDER_VERSION 0xc102b17652bc1e20

// Function table for services hosted by an svchost.
typedef struct zx_service_ops {
    // Opportunity to do on-load work.
    //
    // Called ony once, before any other ops are called. The service may
    // optionally return a context pointer to be passed to the other service ops.
    zx_status_t (*init)(void** out_ctx);

    // Connect to the service with the given name.
    //
    // |ctx| is the pointer returned by |init|, if any.
    //
    // |async| is the async dispatch on which the service provider should
    // schedule its work. This dispatcher might be shared with other service
    // providers.
    //
    // |service_name| is the name of the service to which the client wishes to
    // connect. If the service provider doesn't implement a service with this
    // name, this function should return |ZX_ERR_NOT_SUPPORTED|.
    //
    // This function takes ownership of |request| and should close |request| on
    // error.
    zx_status_t (*connect)(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request);

    // Called before the service is unloaded.
    //
    // |ctx| is the pointer returned by |init|, if any.
    void (*release)(void* ctx);
} zx_service_ops_t;

// Metadata and operations for a service provider.
typedef struct zx_service_provider {
    // A magic number that identifies the ABI.
    uint64_t version; // SERVICE_PROVIDER_VERSION

    // The services that this service provider implements.
    //
    // Represented as a null-terminated array of null-terminated strings.
    const char* const* services;

    // The function table of operations implemented by this service provider.
    const zx_service_ops_t* ops;
} zx_service_provider_t;

__END_CDECLS

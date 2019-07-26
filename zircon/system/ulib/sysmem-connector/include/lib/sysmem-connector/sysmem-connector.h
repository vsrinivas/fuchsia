// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The client of sysmem-connector shouldn't need to know how large this struct
// is (or even whether the pointer is to a heap allocation vs. within a heap
// allocation), so it's declared here, but defined in sysmem-connector.cpp.
typedef struct sysmem_connector sysmem_connector_t;

// Allocate and initialize a sysmem_connector_t.  ZX_OK from this function
// doesn't guarantee that the sysmem driver is found yet, only that the
// connector has successfully been created and initialized.
//
// |sysmem_device_path| must remain valid for lifetime of sysmem_connector_t.
// This is the path to the directory of sysmem device instances (just one
// device instance will actually exist, unless something is going wrong).
__EXPORT zx_status_t sysmem_connector_init(const char* sysmem_directory_path,
                                           sysmem_connector_t** out_connector);

// allocator2_request is consumed.  A call to this function doesn't guarantee
// that the request will reach the sysmem driver, only that the connector has
// queued the request internally to be sent.
//
// If the sysmem driver can't be contacted for an extended duration, the request
// may sit in the queue for that duration - there isn't a timeout, because that
// would probably do more harm than good, since sysmem is always supposed to be
// running.
__EXPORT void sysmem_connector_queue_connection_request(sysmem_connector_t* connector,
                                                        zx_handle_t allocator2_request);

// This call is not allowed to fail.
__EXPORT void sysmem_connector_release(sysmem_connector_t* connector);

__END_CDECLS

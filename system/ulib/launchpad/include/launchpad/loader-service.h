// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// These routines provide loader service implementations that
// some users of liblaunchpad may need.  They are compatible with
// dl_set_loader_service() and are primarily used by devmgr (to
// provide the default system loader service) and clients of
// launchpad that need a specialized variant loader.
//
// Terms:
// "loader service": A channel that speaks the protocol expected by
//     dl_set_loader_service(). The service behind the channel receives
//     load requests (e.g., "libhid.so") and returns VMOs that contain
//     the data associated with that name.
// "local loader service": An in-process loader service.
// "system loader service": A loader service, provided by the system,
//     that is shared by multiple processes.

// Type of the hook for loader_service.  The first argument is
// the one passed to loader_service_simple(), and the second specifies
// which load service was requested (the opcode from zircon/loader.fidl).
// The remaining arguments' meaning depends on the opcode.
typedef zx_status_t (*loader_service_fn_t)(void* loader_arg, uint32_t load_cmd,
                                           zx_handle_t request_handle, const char* file,
                                           zx_handle_t* out);

// Obtain the default loader service for this process.
// That is normally a new connection to the service that
// was used to load this process, if allowed and available.
// Otherwise an in-process loader service, using the filesystem
// will be created.
zx_status_t loader_service_get_default(zx_handle_t* out);

// Create a simple single-threaded loader service, which
// will use the provided service_fn to process load commands
zx_status_t loader_service_simple(loader_service_fn_t loader,
                                  void* loader_arg, zx_handle_t* out);

typedef struct loader_service loader_service_t;

// Create a new file-system backed loader service capable of handling
// any number of clients.
zx_status_t loader_service_create_fs(const char* name, loader_service_t** out);

// Returns a new dl_set_loader_service-compatible loader service channel.
zx_status_t loader_service_connect(loader_service_t* svc, zx_handle_t* out);

// Same as connect except caller provides the channel endpoint (which
// is connected on success, closed on failure)
zx_status_t loader_service_attach(loader_service_t* svc, zx_handle_t channel);

typedef struct loader_service_ops {
    // attempt to load a DSO from suitable library paths
    zx_status_t (*load_object)(void* ctx, const char* name, zx_handle_t* vmo);

    // attempt to load a script interpreter or debug config file
    zx_status_t (*load_abspath)(void* ctx, const char* path, zx_handle_t* vmo);

    // attempt to publish a data sink
    // takes ownership of the provided vmo on both success and failure
    zx_status_t (*publish_data_sink)(void* ctx, const char* name, zx_handle_t vmo);
} loader_service_ops_t;

// Create a loader service backed by custom loader ops
zx_status_t loader_service_create(const char* name,
                                  const loader_service_ops_t* ops, void* ctx,
                                  loader_service_t** out);

// the default publish_data_sink implementation, which publishes
// into /tmp, provided the fs there supports such publishing
zx_status_t loader_service_publish_data_sink_fs(const char* name, zx_handle_t vmo);

__END_CDECLS

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct async_dispatcher async_dispatcher_t; // From <lib/async/dispatcher.h>

// These functions provide an implementation of the shared library loading
// service.  See <zircon/loader.fidl> for a definition of the protocol.
//
// These implementations are compatible with |dl_set_loader_service| and are
// primarily used by devmgr, fshost, and appmgr to provide shared libraries to
// the processes they create.
//
// Terms:
//
// "loader service": A channel that speaks the protocol expected by
//     dl_set_loader_service(). The service behind the channel receives
//     load requests (e.g., "libhid.so") and returns VMOs that contain
//     the data associated with that name.
// "system loader service": A loader service, provided by the system,
//     that is shared by multiple processes.

typedef struct loader_service loader_service_t;

// Create a new file-system backed loader service capable of handling
// any number of clients.
//
// Requests will be processed on the given |async|. If |async| is NULL, this
// library will create a new thread and listen for requests on that thread.
zx_status_t loader_service_create_fs(async_dispatcher_t* dispatcher, loader_service_t** out);

// Create a new file-descriptor backed loader service capable of handling any
// number of clients.
//
// Requests will be processed on the given |async|. If |async| is NULL, this
// library will create a new thread and listen for requests on that thread.
// Paths and objects will be loaded relative to |root_dir_fd| and data will be
// published relative to |data_sink_dir_fd|; the two file descriptors
// are consumed on success.
zx_status_t loader_service_create_fd(async_dispatcher_t* dispatcher,
                                     int root_dir_fd,
                                     int data_sink_dir_fd,
                                     loader_service_t** out);

// Returns a new dl_set_loader_service-compatible loader service channel.
zx_status_t loader_service_connect(loader_service_t* svc, zx_handle_t* out);

// Same as connect except caller provides the channel endpoint (which
// is connected on success, closed on failure).
zx_status_t loader_service_attach(loader_service_t* svc, zx_handle_t channel);

typedef struct loader_service_ops {
    // attempt to load a shared library from suitable library paths.
    zx_status_t (*load_object)(void* ctx, const char* name, zx_handle_t* vmo);

    // attempt to load a script interpreter or debug config file
    zx_status_t (*load_abspath)(void* ctx, const char* path, zx_handle_t* vmo);

    // attempt to publish a data sink
    // takes ownership of the provided vmo on both success and failure.
    zx_status_t (*publish_data_sink)(void* ctx, const char* name, zx_handle_t vmo);

    // finalize the loader service (optional)
    // called shortly before the loader service is destroyed
    void (*finalizer)(void* ctx);
} loader_service_ops_t;

// Create a loader service backed by custom loader ops.
//
// Requests will be processed on the given |async|. If |async| is NULL, this
// library will create a new thread and listen for requests on that thread.
zx_status_t loader_service_create(async_dispatcher_t* dispatcher,
                                  const loader_service_ops_t* ops,
                                  void* ctx,
                                  loader_service_t** out);

// After this function returns, |svc| will destroy itself once there are no
// longer any outstanding connections.
//
// The |finalizer| in |loader_service_ops_t| will be called shortly before |svc|
// destroys itself.
zx_status_t loader_service_release(loader_service_t* svc);
__END_CDECLS

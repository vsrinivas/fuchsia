// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

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
// which load service was requested (the opcode from mx_loader_svc_msg_t).
// The remaining arguments' meaning depends on the opcode.
typedef mx_status_t (*loader_service_fn_t)(void* loader_arg, uint32_t load_cmd,
                                           mx_handle_t request_handle, const char* file,
                                           mx_handle_t* out);


// Obtain a handle to the system loader service, if possible
mx_status_t loader_service_get_system(mx_handle_t* out);

// Obtain the default loader service for this process
// Normally it attempts to use the system loader service, and
// if that fails attempts to create a process-local service
// (which depends on the process having filesystem access
// to executables and libraries needing loading)
mx_status_t loader_service_get_default(mx_handle_t* out);

// After this function returns, loader_service_get_default will no
// longer attempt to use the system loader service for the current
// process. Should only be used by the system loader service itself.
void loader_service_force_local(void);


// Create a simple single-threaded loader service, which
// will use the provided service_fn to process load commands
mx_status_t loader_service_simple(loader_service_fn_t loader,
                                  void* loader_arg, mx_handle_t* out);


typedef struct loader_service loader_service_t;

// Create a new file-system backed loader service capable of handling
// any number of clients.
mx_status_t loader_service_create_fs(const char* name, loader_service_t** out);

// Returns a new dl_set_loader_service-compatible loader service channel.
mx_status_t loader_service_connect(loader_service_t* svc, mx_handle_t* out);

// Same as connect except caller provides the channel endpoint (which
// is connected on success, closed on failure)
mx_status_t loader_service_attach(loader_service_t* svc, mx_handle_t channel);

typedef struct loader_service_ops {
    // attempt to load a DSO from suitable library paths
    mx_status_t (*load_object)(void* ctx, const char* name, mx_handle_t* vmo);

    // attempt to load a script interpreter or debug config file
    mx_status_t (*load_abspath)(void* ctx, const char* path, mx_handle_t* vmo);

    // attempt to publish a data sink
    // takes ownership of the provided vmo on both success and failure
    mx_status_t (*publish_data_sink)(void* ctx, const char* name, mx_handle_t vmo);
} loader_service_ops_t;

// Create a loader service backed by custom loader ops
mx_status_t loader_service_create(const char* name,
                                  const loader_service_ops_t* ops, void* ctx,
                                  loader_service_t** out);

// the default publish_data_sink implementation, which publishes
// into /tmp, provided the fs there supports such publishing
mx_status_t loader_service_publish_data_sink_fs(const char* name, mx_handle_t vmo);

__END_CDECLS

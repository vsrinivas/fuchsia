// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// These routines are "internal" to mxio but are also used by devmgr.
// They implement loader services that are compatible with
// dl_set_loader_service().
//
// Terms:
// "loader service": A channel that speaks the protocol expected by
//     dl_set_loader_service(). The service behind the channel receives
//     load requests (e.g., "libhid.so") and returns VMOs that contain
//     the data associated with that name.
// "local loader service": An in-process loader service.
// "system loader service": A loader service, provided by the system,
//     that is shared by multiple processes.

// Type of the hook for mxio_loader_service.  The first argument is
// the one passed to mxio_loader_service, and the second specifies
// which load service was requested (the opcode from mx_loader_svc_msg_t).
// The remaining arguments' meaning depends on the opcode.
typedef mx_handle_t (*mxio_loader_service_function_t)
                        (void* loader_arg,
                         uint32_t load_cmd,
                         mx_handle_t request_handle,
                         const char* file);

// Start a thread to resolve loader service requests and return a
// channel handle to talk to said service.  If the function passed
// is NULL, a default implementation that reads from the filesystem is
// used.  Will try to use the system loader service if available.
mx_handle_t mxio_loader_service(mxio_loader_service_function_t loader,
                                void* loader_arg);

// After this function returns, mxio_loader_service will no longer
// attempt to use the system loader service for the current process.
// Should only be used by the system loader service itself.
void mxio_force_local_loader_service(void);

// A multiloader provides multiple loader service channels that share a
// single mxio_dispatcher and use a filesystem-based loading scheme.
typedef struct mxio_multiloader mxio_multiloader_t;

// Creates a new multiloader. |name| is copied and used for internal
// thread names.
mx_status_t mxio_multiloader_create(const char* name,
                                    mxio_multiloader_t** ml_out);

// Returns a new dl_set_loader_service-compatible loader service channel.
mx_handle_t mxio_multiloader_new_service(mxio_multiloader_t* ml);

__END_CDECLS

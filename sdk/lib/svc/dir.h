// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SVC_DIR_H_
#define LIB_SVC_DIR_H_

#include <lib/async/dispatcher.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef void(svc_connector_t)(void* context, const char* service_name, zx_handle_t service_request)
    ZX_AVAILABLE_SINCE(1);

typedef struct svc_dir svc_dir_t ZX_AVAILABLE_SINCE(1);

// During Fuchsia API level 10, this library went through an API overhaul. It was simplified,
// removing redundancy and using clearer function and parameter names. Since there was only one
// client of this library, the C++ library |component::OutgoingDirectory|, it was safe to break the
// API of this library in the latest API level at the time (level 10). Accordingly, there's a split
// between the legacy API which is supported in API levels < 10 and the overhaul which was
// introduced in API level 10. Users should use the new API, as the older API will be deleted once
// Fuchsia API level 9 is no longer supported.

// Create an outgoing directory object in which service and directory nodes can be installed.
// This currently cannot fail, and returns ZX_OK.
__EXPORT zx_status_t svc_directory_create(svc_dir_t** result) ZX_AVAILABLE_SINCE(10);

// Serve |dir| on the provided |request| handle. This function should
// only be called once and should be done after the directory has been populated.
__EXPORT zx_status_t svc_directory_serve(svc_dir_t* dir, async_dispatcher_t* dispatcher,
                                         zx_handle_t request) ZX_AVAILABLE_SINCE(10);

// Adds a service named |name| to the given |dir| in the provided
// |path|.
//
// |path| should be a directory path delimited by "/". No leading nor trailing
// slash is allowed. If one is encountered, this function will return an
// error. If the path is empty or NULL, then the service will be installed
// under the root of |dir|.
//
// When a client requests the service, |handler| will be called on the async_t
// passed to |svc_dir_create|. The |context| will be passed to |handler| as its
// first argument.
//
// This may fail in the following ways:
//
// If an entry with the given |name| already exists, this returns ZX_ERR_ALREADY_EXISTS.
// If |name| is invalid, then ZX_ERR_INVALID_ARGS is returned.
// If |path| is malformed, then ZX_ERR_INVALID_ARGS is returned.
// Otherwise, this returns ZX_OK.
__EXPORT zx_status_t svc_directory_add_service(svc_dir_t* dir, const char* path, size_t path_size,
                                               const char* name, size_t name_size, void* context,
                                               svc_connector_t handler) ZX_AVAILABLE_SINCE(10);

// Add a subdirectory named |name| to the given |dir| in the provided |path|.
//
// |subdir| should be a handle to a client end for a |fuchsia.io.Directory|
// connection. When |dir| receives requests for |name|, it will forwards all
// requests to this handle.
//
// |path| should be a directory path delimited by "/". No leading nor trailing
// slash is allowed. If one is encountered, this function will return an
// error. If the path is empty or NULL, then the service will be installed
// under the root of |dir|.
//
// This may fail in the following ways:
//
// If |dir| or |name| is NULL, or |subdir| is an invalid handle, then ZX_ERR_INVALID_ARGS is
// returned. If an entry already exists under |name|, then ZX_ERR_ALREADY_EXISTS is returned.
__EXPORT zx_status_t svc_directory_add_directory(svc_dir_t* dir, const char* path, size_t path_size,
                                                 const char* name, size_t name_size,
                                                 zx_handle_t subdir) ZX_AVAILABLE_SINCE(10);

// Remove the entry named |name| from the provided |path| under
// the given |dir|.
//
// This may fail in the following ways:
// If the entry does not exist, then ZX_ERR_NOT_FOUND is returned.
// If |path| is malformed, or if either |path| or |name| is NULL, then ZX_ERR_INVALID_ARGS is
// returned.
__EXPORT zx_status_t svc_directory_remove_entry(svc_dir_t* dir, const char* path, size_t path_size,
                                                const char* name, size_t name_size)
    ZX_AVAILABLE_SINCE(10);

// Destroy the provided directory. This currently cannot fail, and
// returns ZX_OK.
__EXPORT zx_status_t svc_directory_destroy(svc_dir_t* dir) ZX_AVAILABLE_SINCE(10);

// All the functions listed below are deprecated as of Fuchsia API level 10. They should not be used
// by new clients.

__EXPORT zx_status_t svc_dir_create(async_dispatcher_t* dispatcher, zx_handle_t directory_request,
                                    svc_dir_t** out_result)
    ZX_REMOVED_SINCE(
        /*added=*/1, /*deprecated=*/10, /*removed=*/10,
        "Use |svc_directory_create|. The new function does not serve the directory automatically. Instead use |svc_directory_serve| afterwards.");

__EXPORT zx_status_t svc_dir_create_without_serve(svc_dir_t** result)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10, "Use |svc_directory_create|");

__EXPORT zx_status_t svc_dir_destroy(svc_dir_t* dir)
    ZX_REMOVED_SINCE(/*added=*/1, /*deprecated=*/10, /*removed=*/10, "Use |svc_directory_destroy|");

__EXPORT zx_status_t svc_dir_serve(svc_dir_t* dir, async_dispatcher_t* dispatcher,
                                   zx_handle_t request)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10, "Use |svc_directory_serve|");

__EXPORT zx_status_t svc_dir_add_service(svc_dir_t* dir, const char* type, const char* service_name,
                                         void* context, svc_connector_t* handler)
    ZX_REMOVED_SINCE(/*added=*/1, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_add_service|");

__EXPORT zx_status_t svc_dir_add_service_by_path(svc_dir_t* dir, const char* path,
                                                 const char* service_name, void* context,
                                                 svc_connector_t* handler)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_add_service|");

__EXPORT zx_status_t svc_dir_add_directory(svc_dir_t* dir, const char* name, zx_handle_t subdir)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_add_directory|");

__EXPORT zx_status_t svc_dir_add_directory_by_path(svc_dir_t* dir, const char* path,
                                                   const char* name, zx_handle_t subdir)
    ZX_REMOVED_SINCE(/*added=*/9, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_add_directory|");

__EXPORT zx_status_t svc_dir_remove_service(svc_dir_t* dir, const char* type,
                                            const char* service_name)
    ZX_REMOVED_SINCE(/*added=*/1, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_remove_entry|");

__EXPORT zx_status_t svc_dir_remove_service_by_path(svc_dir_t* dir, const char* path,
                                                    const char* service_name)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_remove_entry|");

__EXPORT zx_status_t svc_dir_remove_directory(svc_dir_t* dir, const char* name)
    ZX_REMOVED_SINCE(/*added=*/7, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_remove_entry|");

__EXPORT zx_status_t svc_dir_remove_entry_by_path(svc_dir_t* dir, const char* path,
                                                  const char* name)
    ZX_REMOVED_SINCE(/*added=*/9, /*deprecated=*/10, /*removed=*/10,
                     "Use |svc_directory_remove_entry|");

__END_CDECLS

#endif  // LIB_SVC_DIR_H_

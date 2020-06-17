// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Connects to a service at the given |path|.
//
// The |path| is looked up in the namespace for the current process. If found in
// the namespace, the object at the path is opened, passing the |request| to
// the remote party.
//
// Upon success, the |request| is handed off to the remote party. The operation
// completes asynchronously, which means a ZX_OK result does not ensure that the
// requested service actually exists.
//
// |request| must be a channel.
//
// Always consumes |request|.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |path| is invalid.
//
// ZX_ERR_NOT_FOUND: A prefix of |path| cannot be found in the namespace for the
// current process.
//
// ZX_ERR_NOT_SUPPORTED: The requested |path| was found in the namespace for the
// current process but the namespace entry does not support connecting to
// services.
//
// ZX_ERR_ACCESS_DENIED: The namespace entry has insufficient rights to connect
// to services.
zx_status_t fdio_service_connect(const char* path, zx_handle_t request);

// Connects to a service at the given |path| relative to the given |directory|.
//
// Upon success, the |request| is handed off to the remote party. The operation
// completes asynchronously, which means a ZX_OK result does not ensure that the
// requested service actually exists.
//
// |directory| must be a channel that implements the |fuchsia.io.Directory|
// protocol.
//
// |request| must be a channel.
//
// Always consumes |request|.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |directory| or |path| is invalid.
//
// ZX_ERR_ACCESS_DENIED: |directory| has insufficient rights to connect to
// services.
zx_status_t fdio_service_connect_at(zx_handle_t directory, const char* path, zx_handle_t request);

// Opens the remote object at the given |path| relative to the root of the namespace with the given
// |flags| asynchronously.
//
// |flags| is a bit field of |fuchsia.io.OPEN_*|.
//
// Always consumes |request|.
//
// See |fdio_service_connect| for details.
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t request);

// Opens the remote object at the given |path| relative to the given |directory| with the given
// |flags| asynchronously.
//
// |flags| is a bit field of |fuchsia.io.OPEN_*|.
//
// Always consumes |request|.
//
// See |fdio_service_connect_at| for details.
zx_status_t fdio_open_at(zx_handle_t directory, const char* path, uint32_t flags,
                         zx_handle_t request);

// Opens the remote object at the given |path| relative to the root of the namespace with the given
// |flags| synchronously, and on success, binds that channel to a file descriptor, returned via
// |out_fd|.
//
// Note that unlike fdio_open, this function is synchronous. This is because it produces a file
// descriptor, which requires synchronously waiting for the open to complete.
//
// |flags| is a bit field of |fuchsia.io.OPEN_*|.
//
// See |fdio_service_connect| for details.
zx_status_t fdio_open_fd(const char* path, uint32_t flags, int* out_fd);

// Opens the remote object at the given |path| relative to the given |dir_fd| with the given |flags|
// synchronously, and on success, binds that channel to a file descriptor, returned via |out_fd|.
//
// Note that unlike fdio_open, this function is synchronous. This is because it produces a file
// descriptor, which requires synchronously waiting for the open to complete.
//
// |flags| is a bit field of |fuchsia.io.OPEN_*|.
//
// See |fdio_service_connect| fort details.
zx_status_t fdio_open_fd_at(int dir_fd, const char* path, uint32_t flags, int* out_fd);

// Clone the given |node| asynchronously.
//
// |node| must be a channel that implements the |fuchsia.io.Node| protocol.
//
// Upon success, returns a handle to a newly created channel whose remote
// endpoint has been sent to |node| as a request for a clone.
//
// The |node| is cloned as readable and writable.
//
// Upon failure, returns |ZX_HANDLE_INVALID|.
zx_handle_t fdio_service_clone(zx_handle_t node);

// Requests that |request| be connected to a clone of the given |node|
// asynchronously.
//
// |node| must be a channel that implements the |fuchsia.io.Node| protocol.
//
// |request| must be a channel.
//
// Upon success, |request| has been sent to |node| as a request for a clone.
//
// The |node| is cloned as readable and writable.
//
// # Errors
//
// |ZX_ERR_INVALID_ARGS|: |node| or |request| is invalid.
//
// Returns transport- and application-level errors associated with
// |fuchsia.io.Node/Clone|.
zx_status_t fdio_service_clone_to(zx_handle_t node, zx_handle_t request);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/analyzer.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Connects to a service at `path` relative to the root of the namespace for the current process
// asynchronously.
//
// `request` must be a channel.
//
// Always consumes `request`.
//
// See `fdio_ns_service_connect` for details.
zx_status_t fdio_service_connect(const char* path, ZX_HANDLE_RELEASE zx_handle_t request)
    ZX_AVAILABLE_SINCE(1);

// Connects to a service at the given `path` relative to the given `directory` asynchronously.
//
// Upon success, the `request` is handed off to the remote party. The operation completes
// asynchronously, which means a ZX_OK result does not ensure that the requested service actually
// exists.
//
// `directory` must be a channel that implements the `fuchsia.io/Directory` protocol.
//
// `request` must be a channel. It will always be consumed by this function.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: `directory` or `path` is invalid.
zx_status_t fdio_service_connect_at(zx_handle_t directory, const char* path,
                                    ZX_HANDLE_RELEASE zx_handle_t request) ZX_AVAILABLE_SINCE(1);

// Connect to a service named `name` in /svc.
zx_status_t fdio_service_connect_by_name(const char* name, ZX_HANDLE_RELEASE zx_handle_t request)
    ZX_AVAILABLE_SINCE(1);

// Opens an object at `path` relative to the root of the namespace for the current process with
// `flags` asynchronously.
//
// `flags` is a `fuchsia.io/OpenFlags`.
//
// Always consumes `request`.
//
// See `fdio_ns_open` for details.
zx_status_t fdio_open(const char* path, uint32_t flags, ZX_HANDLE_RELEASE zx_handle_t request)
    ZX_AVAILABLE_SINCE(1);

// Opens an object at `path` relative to `directory` with `flags` asynchronously.
//
// Upon success, `request` is handed off to the remote party. The operation completes
// asynchronously, which means a ZX_OK result does not ensure that the requested service actually
// exists.
//
// `directory` must be a channel that implements the `fuchsia.io/Directory` protocol.
//
// `request` must be a channel which will always be consumed by this function.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: `directory` or `path` is invalid.
zx_status_t fdio_open_at(zx_handle_t directory, const char* path, uint32_t flags,
                         ZX_HANDLE_RELEASE zx_handle_t request) ZX_AVAILABLE_SINCE(1);

// Opens an object at `path` relative to the root of the namespace for the current process with
// `flags` synchronously, and on success, binds that channel to a file descriptor, returned via
// `out_fd`.
//
// Note that unlike `fdio_open`, this function is synchronous. This is because it produces a file
// descriptor, which requires synchronously waiting for the open to complete.
//
// `flags` is a `fuchsia.io/OpenFlags`.
//
// See `fdio_open` for details.
zx_status_t fdio_open_fd(const char* path, uint32_t flags, int* out_fd) ZX_AVAILABLE_SINCE(1);

// Opens an object at `path` relative to `dir_fd` with `flags` synchronously, and on success, binds
// that channel to a file descriptor, returned via `out_fd`.
//
// Note that unlike fdio_open, this function is synchronous. This is because it produces a file
// descriptor, which requires synchronously waiting for the open to complete.
//
// `flags` is a `fuchsia.io/OpenFlags`.
//
// See `fdio_open_at` fort details.
zx_status_t fdio_open_fd_at(int dir_fd, const char* path, uint32_t flags, int* out_fd)
    ZX_AVAILABLE_SINCE(1);

// Clone the given `node` asynchronously.
//
// `node` must be a channel that implements the `fuchsia.io/Node` protocol.
//
// Upon success, returns a handle to a newly created channel whose remote endpoint has been sent to
// `node` as a request for a clone.
//
// The `node` is cloned as readable and writable.
//
// Upon failure, returns `ZX_HANDLE_INVALID`.
zx_handle_t fdio_service_clone(ZX_HANDLE_USE zx_handle_t node) ZX_DEPRECATED_SINCE(
    1, 10,
    "Incorrectly assumes all file descriptors are backed by fuchsia.io/Node. Use fdio_clone_fd instead.");

// Requests that `request` be connected to a clone of the given `node` asynchronously.
//
// `node` must be a channel that implements the `fuchsia.io/Node` protocol.
//
// `request` must be a channel.
//
// Upon success, `request` has been sent to `node` as a request for a clone. The `node` is cloned as
// readable and writable.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `node` or `request` is invalid.
//
// Returns transport- and application-level errors associated with
// `fuchsia.io/Node.Clone`.
zx_status_t fdio_service_clone_to(ZX_HANDLE_USE zx_handle_t node,
                                  ZX_HANDLE_RELEASE zx_handle_t request)
    ZX_DEPRECATED_SINCE(
        1, 10,
        "Incorrectly assumes all file descriptors are backed by fuchsia.io/Node. Use fdio_clone_fd instead.");

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_DIRECTORY_H_

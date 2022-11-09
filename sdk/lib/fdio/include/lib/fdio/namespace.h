// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_

#include <stdint.h>
#include <zircon/availability.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct fdio_namespace fdio_ns_t;

// Create a new, empty namespace
zx_status_t fdio_ns_create(fdio_ns_t** out) ZX_AVAILABLE_SINCE(1);

// Destroy and deallocate a namespace.
//
// If the namespace is in-use, it will be destroyed once it is no longer referenced.
//
// This function always returns `ZX_OK`.
zx_status_t fdio_ns_destroy(fdio_ns_t* ns) ZX_AVAILABLE_SINCE(1);

// Create a new directory within a namespace, bound to the directory-protocol-compatible handle h
//
// The path must be an absolute path like "/x/y/z". It is relative to the root
// of the namespace.
//
// Ownership of `h` is transferred to `ns`: it is closed on error.
//
// # Errors
//
//   * `ZX_ERR_BAD_STATE`: Namespace is already in use and immutable.
//
//   * `ZX_ERR_ALREADY_EXISTS`: There is already a mounted directory there.
//
//   * `ZX_ERR_NOT_SUPPORTED`: `path` would shadow a mounted directory.
//
//   * `ZX_ERR_INVALID_ARGS`: `path` is null or is not an absolute path.
//
//   * `ZX_ERR_BAD_PATH`: `path` is not a valid path.
zx_status_t fdio_ns_bind(fdio_ns_t* ns, const char* path, zx_handle_t h) ZX_AVAILABLE_SINCE(1);

// Unbinds `path` from a namespace, closing the handle within `ns` that corresponds to that path
// when all references to the node go out of scope.
//
// # Errors
//
//   * `ZX_ERR_NOT_FOUND: `path` is not a remote.
//
//   * `ZX_ERR_NOT_SUPPORTED: `path` is the root of the namespace.
//
//   * `ZX_ERR_INVALID_ARGS: `path` is null or not an absolute path.
//
//   * `ZX_ERR_BAD_PATH: `path` is not a valid path.
zx_status_t fdio_ns_unbind(fdio_ns_t* ns, const char* path) ZX_AVAILABLE_SINCE(1);

// Whether `path` is bound to a remote directory in `ns`.
bool fdio_ns_is_bound(fdio_ns_t* ns, const char* path) ZX_AVAILABLE_SINCE(1);

// Create a new directory within a namespace, bound to the
// directory referenced by the file descriptor fd.
// The path must be an absolute path like "/x/y/z".  It is relative to the root
// of the namespace.
//
// `fd` is borrowed by this function, but is not closed on success or error.
// Closing the fd after success does not affect namespace.
//
// # Errors
//
//   * `ZX_ERR_BAD_STATE: Namespace is already in use and immutable or `fd` cannot be cloned in its
//     current state.
//
//   * `ZX_ERR_ALREADY_EXISTS: There is already a mounted directory there.
//
//   * `ZX_ERR_NOT_SUPPORTED: `path` would shadow a mounted directory or `fd` cannot
//     be represented as a handle.
//
//   * `ZX_ERR_INVALID_ARGS: `path` is null or is not an absolute path or `fd` is not
//     a valid file descriptor.
//
//   * `ZX_ERR_BAD_PATH: `path` is not a valid path.
//
//   * `ZX_ERR_ACCESS_DENIED: `fd` has insufficient rights to clone the underlying
//     object.
zx_status_t fdio_ns_bind_fd(fdio_ns_t* ns, const char* path, int fd) ZX_AVAILABLE_SINCE(1);

// Opens the root directory of the namespace as a file descriptor
int fdio_ns_opendir(fdio_ns_t* ns) ZX_AVAILABLE_SINCE(1);

// Changes the current directory to "/" in the provided namespace.
zx_status_t fdio_ns_chdir(fdio_ns_t* ns) ZX_AVAILABLE_SINCE(1);

// Retrieve the fdio "global" namespace (if any).
zx_status_t fdio_ns_get_installed(fdio_ns_t** ns) ZX_AVAILABLE_SINCE(1);

// Contains parallel arrays of handles, path names, and types.
typedef struct fdio_flat_namespace {
  // The number of elements in the other arrays of this struct.
  size_t count;

  // handle[i] is the zx_handle_t representing that element in the namespace.
  zx_handle_t* handle;

  // type[i] is a handle info entry as defined in zircon/processargs.h by PA_HND.
  uint32_t* type;

  // path[i] is the user-readable name of that element (e.g., "/bin").
  const char* const* path;
} fdio_flat_namespace_t ZX_AVAILABLE_SINCE(1);

// On success the caller takes ownership of a fdio_flat_namespace_t containing a flat representation
// of the exported namespace (the one provided in 'ns' or the active root namespace, respectively.)
// The handles are CLONEs of the handles in the namespace and also belong to the caller.
//
// The whole data structure can be released with fdio_ns_free_flat_ns().
zx_status_t fdio_ns_export(fdio_ns_t* ns, fdio_flat_namespace_t** out) ZX_AVAILABLE_SINCE(1);
zx_status_t fdio_ns_export_root(fdio_flat_namespace_t** out) ZX_AVAILABLE_SINCE(1);

// Attempt to connect to a service through the namespace. The handle is always consumed.  It will be
// closed on error or passed to the remote service on success. The path must be an absolute path
// starting with "/".
zx_status_t
fdio_ns_connect(fdio_ns_t* ns, const char* path, uint32_t flags, zx_handle_t request) ZX_DEPRECATED_SINCE(
    1, 8,
    "Incorrectly named due to accepting flags. Use fdio_ns_open or fdio_ns_service_connect instead.");

// Opens an object at `path` relative to the root of `ns` with `flags` asynchronously.
//
// `path` is looked up in `ns`. If found, the object at `path` is opened, passing `request` to the
// remote party.
//
// Upon success, `request` is handed off to the remote party. The operation completes
// asynchronously, which means a ZX_OK result does not ensure that the requested service actually
// exists.
//
// `path` must be absolute.
//
// `flags` is a `fuchsia.io/OpenFlags`.
//
// `request` must be a channel and it is always consumed by this function.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `path` is invalid.
//
//   * `ZX_ERR_NOT_FOUND`: A prefix of `path` cannot be found in `ns`.
zx_status_t fdio_ns_open(fdio_ns_t* ns, const char* path, uint32_t flags, zx_handle_t request)
    ZX_AVAILABLE_SINCE(8);

// Connects to a service at `path` relative to the root of `ns` asynchronously.
//
// `path` is looked up in `ns`. If found, the object at `path` is opened, passing `request` to the
// remote party.
//
// Upon success, `request` is handed off to the remote party. The operation completes
// asynchronously, which means a ZX_OK result does not ensure that the requested service actually
// exists.
//
// `path` must be absolute.
//
// `request` must be a channel and it is always consumed by this function.
//
// # Errors
//
//   * `ZX_ERR_INVALID_ARGS`: `path` is invalid.
//
//   * `ZX_ERR_NOT_FOUND`: A prefix of `path` cannot be found in `ns`.
zx_status_t fdio_ns_service_connect(fdio_ns_t* ns, const char* path, zx_handle_t request)
    ZX_AVAILABLE_SINCE(8);

// Frees a flat namespace.
//
// Closes all handles contained within `ns`.
void fdio_ns_free_flat_ns(fdio_flat_namespace_t* ns) ZX_AVAILABLE_SINCE(1);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_

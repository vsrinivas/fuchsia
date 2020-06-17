// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_
#define LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct fdio_namespace fdio_ns_t;

// Create a new, empty namespace
zx_status_t fdio_ns_create(fdio_ns_t** out);

// Destroy and deallocate a namespace.
//
// If the namespace is in-use, it will be destroyed once it is
// no longer referenced.
//
// This function always returns |ZX_OK|.
zx_status_t fdio_ns_destroy(fdio_ns_t* ns);

// Create a new directory within a namespace, bound to the
// directory-protocol-compatible handle h
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// Ownership of |h| is transferred to |ns|: it is closed on error.
zx_status_t fdio_ns_bind(fdio_ns_t* ns, const char* path, zx_handle_t h);

// Unbinds |path| from a namespace, closing the handle within |ns| that
// corresponds to that path when all references to the node go out of scope.
//
// Returns ZX_ERR_NOT_FOUND if |path| is not a remote.
// Returns ZX_ERR_NOT_SUPPORTED if |path| is the root of the namespace.
// Returns ZX_ERR_INVALID_ARGS if |path| is otherwise invalid.
zx_status_t fdio_ns_unbind(fdio_ns_t* ns, const char* path);

// Create a new directory within a namespace, bound to the
// directory referenced by the file descriptor fd.
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// |fd| is borrowed by this function, but is not closed on success or error.
// Closing the fd after success does not affect namespace.
//
// Failures:
// ZX_ERR_BAD_STATE: Namespace is already in use and immutable.
// ZX_ERR_ALREADY_EXISTS: There is already a mounted directory there.
// ZX_ERR_NOT_SUPPORTED: This path would shadow a mounted directory.
zx_status_t fdio_ns_bind_fd(fdio_ns_t* ns, const char* path, int fd);

// Open the root directory of the namespace as a file descriptor
int fdio_ns_opendir(fdio_ns_t* ns);

// chdir to / in the provided namespace
zx_status_t fdio_ns_chdir(fdio_ns_t* ns);

// Retrieve the fdio "global" namespace (if any).
zx_status_t fdio_ns_get_installed(fdio_ns_t** ns);

// flat_namespace contains parallel arrays of handles, path names, and types.  The number of
// elements of these arrays is given by |count|.  For any given offset i:
// - handle[i] is the zx_handle_t representing that element in the namespace
// - path[i] is the user-readable name of that element (e.g., "/bin")
// - type[i] is a handle info entry as defined in zircon/processargs.h by PA_HND.
typedef struct fdio_flat_namespace {
  size_t count;
  zx_handle_t* handle;
  uint32_t* type;
  const char* const* path;
} fdio_flat_namespace_t;

// On success the caller takes ownership of a fdio_flat_namespace_t
// containing a flat representation of the exported namespace (the
// one provided in 'ns' or the active root namespace, respectively.)
// The handles are CLONEs of the handles in the namespace and also
// belong to the caller.
// The whole data structure can be released with fdio_ns_free_flat_ns().
zx_status_t fdio_ns_export(fdio_ns_t* ns, fdio_flat_namespace_t** out);
zx_status_t fdio_ns_export_root(fdio_flat_namespace_t** out);

// Attempt to connect to a service through the namespace.
// The handle is always consumed.  It will be closed on error
// or passed to the remote service on success.
// The path must be an absolute path starting with / and containing
// no ".." or "." or empty segments.
zx_status_t fdio_ns_connect(fdio_ns_t* ns, const char* path, uint32_t zxflags, zx_handle_t h);

// Frees a flat namespace.
// Closes all handles contained within |ns|.
void fdio_ns_free_flat_ns(fdio_flat_namespace_t* ns);

__END_CDECLS

#endif  // LIB_FDIO_INCLUDE_LIB_FDIO_NAMESPACE_H_

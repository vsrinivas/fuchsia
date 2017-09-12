// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct fdio_namespace fdio_ns_t;


// Create a new, empty namespace
zx_status_t fdio_ns_create(fdio_ns_t** out);

// Destroy and deallocate a namespace
// Will fail (ZX_ERR_BAD_STATE) if the namespace is in use.
zx_status_t fdio_ns_destroy(fdio_ns_t* ns);

// Create a new directory within a namespace, bound to the
// directory-protocol-compatible handle h
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// The handle is not closed on failure.
//
// Will fail with ZX_ERR_BAD_STATE if the namespace is in use.
zx_status_t fdio_ns_bind(fdio_ns_t* ns, const char* path, zx_handle_t h);

// Create a new directory within a namespace, bound to the
// directory referenced by the file descriptor fd.
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// The fd is not closed on success or failure.
// Closing the fd after success does not affect namespace.
//
// Will fail with ZX_ERR_BAD_STATE if the namespace is in use.
zx_status_t fdio_ns_bind_fd(fdio_ns_t* ns, const char* path, int fd);

// Open the root directory of the namespace as a file descriptor
int fdio_ns_opendir(fdio_ns_t* ns);

// chdir to / in the provided namespace
zx_status_t fdio_ns_chdir(fdio_ns_t* ns);

// Replace the fdio "global" namespace with the provided namespace
zx_status_t fdio_ns_install(fdio_ns_t* ns);


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
// The whole data structure can be released with free(), keeping in
// mind that the handles should be used or closed first.
zx_status_t fdio_ns_export(fdio_ns_t* ns, fdio_flat_namespace_t** out);
zx_status_t fdio_ns_export_root(fdio_flat_namespace_t** out);

// Attempt to connect to a service through the namespace.
// The handle is always consumed.  It will be closed on error
// or passed to the remote service on success.
// The path must be an absolute path starting with / and containing
// no ".." or "." or empty segments.
zx_status_t fdio_ns_connect(fdio_ns_t* ns, const char* path, zx_handle_t h);
__END_CDECLS;

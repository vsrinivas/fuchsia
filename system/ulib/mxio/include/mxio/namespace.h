// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct mxio_namespace mxio_ns_t;


// Create a new, empty namespace
mx_status_t mxio_ns_create(mxio_ns_t** out);

// Destroy and deallocate a namespace
// Will fail (MX_ERR_BAD_STATE) if the namespace is in use.
mx_status_t mxio_ns_destroy(mxio_ns_t* ns);

// Create a new directory within a namespace, bound to the
// directory-protocol-compatible handle h
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// The handle is not closed on failure.
//
// Will fail with MX_ERR_BAD_STATE if the namespace is in use.
mx_status_t mxio_ns_bind(mxio_ns_t* ns, const char* path, mx_handle_t h);

// Create a new directory within a namespace, bound to the
// directory referenced by the file descriptor fd.
// The path must be an absolute path, like "/x/y/z", containing
// no "." nor ".." entries.  It is relative to the root of the
// namespace.
//
// The fd is not closed on success or failure.
// Closing the fd after success does not affect namespace.
//
// Will fail with MX_ERR_BAD_STATE if the namespace is in use.
mx_status_t mxio_ns_bind_fd(mxio_ns_t* ns, const char* path, int fd);

// Open the root directory of the namespace as a file descriptor
int mxio_ns_opendir(mxio_ns_t* ns);

// chdir to / in the provided namespace
mx_status_t mxio_ns_chdir(mxio_ns_t* ns);

// Replace the mxio "global" namespace with the provided namespace
mx_status_t mxio_ns_install(mxio_ns_t* ns);


typedef struct mxio_flat_namespace {
    size_t count;
    mx_handle_t* handle;
    uint32_t* type;
    const char* const* path;
} mxio_flat_namespace_t;

// On success the caller takes ownership of a mxio_flat_namespace_t
// containing a flat representation of the exported namespace (the
// one provided in 'ns' or the active root namespace, respectively.)
// The handles are CLONEs of the handles in the namespace and also
// belong to the caller.
// The whole data structure can be released with free(), keeping in
// mind that the handles should be used or closed first.
mx_status_t mxio_ns_export(mxio_ns_t* ns, mxio_flat_namespace_t** out);
mx_status_t mxio_ns_export_root(mxio_flat_namespace_t** out);

// Attempt to connect to a service through the namespace.
// The handle is always consumed.  It will be closed on error
// or passed to the remote service on success.
// The path must be an absolute path starting with / and containing
// no ".." or "." or empty segments.
mx_status_t mxio_ns_connect(mxio_ns_t* ns, const char* path, mx_handle_t h);
__END_CDECLS;

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/limits.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
typedef struct {
    fuchsia_io_NodeOnOpenEvent primary;
    fuchsia_io_NodeInfo extra;
} zxfidl_on_open_t;

#define FDIO_MMAP_FLAG_READ    (1u << 0)
#define FDIO_MMAP_FLAG_WRITE   (1u << 1)
#define FDIO_MMAP_FLAG_EXEC    (1u << 2)
// Require a copy-on-write clone of the underlying VMO.
// The request should fail if the VMO is not cloned.
// May not be supplied with FDIO_MMAP_FLAG_EXACT.
#define FDIO_MMAP_FLAG_PRIVATE (1u << 16)
// Require an exact (non-cloned) handle to the underlying VMO.
// The request should fail if a handle to the exact VMO
// is not returned.
// May not be supplied with FDIO_MMAP_FLAG_PRIVATE.
#define FDIO_MMAP_FLAG_EXACT   (1u << 17)

static_assert(FDIO_MMAP_FLAG_READ == ZX_VM_PERM_READ, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_WRITE == ZX_VM_PERM_WRITE, "Vmar / Mmap flags should be aligned");
static_assert(FDIO_MMAP_FLAG_EXEC == ZX_VM_PERM_EXECUTE, "Vmar / Mmap flags should be aligned");

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

#define READDIR_CMD_NONE  0
#define READDIR_CMD_RESET 1

__END_CDECLS

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/compiler.h>
#include <stdint.h>
#include <unistd.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>

__BEGIN_CDECLS

// BEGIN DEPRECATED ------------------------------------------------------------

// Use fdio_cwd_clone, fdio_fd_clone, and fdio_fd_transfer instead.
zx_status_t fdio_clone_fd(int fd, int newfd, zx_handle_t* handles, uint32_t* types);
zx_status_t fdio_transfer_fd(int fd, int newfd, zx_handle_t* handles, uint32_t* types);

// Use fdio_fd_create instead.
zx_status_t fdio_create_fd(zx_handle_t* handles, uint32_t* types, size_t hcount, int* fd_out);

// END DEPRECATED --------------------------------------------------------------

__END_CDECLS

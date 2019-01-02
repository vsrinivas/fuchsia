// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zxs/zxs.h>
#include <zircon/compiler.h>

#include "private.h"

__BEGIN_CDECLS

// Returns a pointer to the |zxs_socket_t| inside the given |fd|, if such a
// struct exists.
//
// Caller receives a reference to the |fdio_t|. The caller is responsible for
// calling fdio_release to balance the reference count.
//
// Returns |NULL| if no |zxs_socket_t| was found.
fdio_t* fd_to_socket(int fd, const zxs_socket_t** out_socket);

__END_CDECLS

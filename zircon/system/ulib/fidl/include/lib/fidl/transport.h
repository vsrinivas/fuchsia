// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_TRANSPORT_H_
#define LIB_FIDL_TRANSPORT_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Writes |capacity| bytes from |buffer| to the control channel of |socket|.
//
// Blocks until |socket| is able to accept a control plane message.
zx_status_t fidl_socket_write_control(zx_handle_t socket, const void* buffer,
                                      size_t capacity);

// Reads |capacity| bytes from the control channel of |socket| to |buffer|.
//
// Blocks until a control plane message is able to be read from |socket|.
//
// The actual number of bytes reads from the control plan is returned in
// |out_actual|.
zx_status_t fidl_socket_read_control(zx_handle_t socket, void* buffer,
                                     size_t capacity, size_t* out_actual);

// Issues a transaction on the control channel of |socket|.
//
// First, writes |capacity| bytes from |buffer| to the control channel of
// |socket|. Second, reads |out_capacity| bytes from the control channel of
// |socket| to |out_buffer|.
//
// Blocks until the transaction is complete.
//
// |buffer| and |out_buffer| may be aliased.
//
// The actual number of bytes reads from the control plan is returned in
// |out_actual|.
zx_status_t fidl_socket_call_control(zx_handle_t socket, const void* buffer,
                                     size_t capacity, void* out_buffer,
                                     size_t out_capacity, size_t* out_actual);

__END_CDECLS

#endif // LIB_FIDL_TRANSPORT_H_

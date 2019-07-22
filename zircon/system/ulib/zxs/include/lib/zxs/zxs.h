// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXS_ZXS_H_
#define LIB_ZXS_ZXS_H_

#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <lib/zx/socket.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct msghdr;
struct sockaddr;

// Flags that describe how the |zxs| library will interact with the kernel
// socket object.
typedef uint32_t zxs_flags_t;

// If set, the socket is used to transport data in atomic chunks.
#define ZXS_FLAG_DATAGRAM ((zxs_flags_t)1u << 0)

// A socket.
typedef struct zxs_socket {
  // The control plane.
  llcpp::fuchsia::posix::socket::Control::SyncClient control;

  // The data plane.
  zx::socket socket;

  // Flags that describe how the |zxs| library will interact with the kernel
  // socket object.
  zxs_flags_t flags;

  // Used to implement SO_RCVTIMEO. See `man 7 socket` for details.
  zx::duration rcvtimeo;

  // Used to implement SO_SNDTIMEO. See `man 7 socket` for details.
  zx::duration sndtimeo;
} zxs_socket_t;

// Closes a |zxs_socket_t|.
//
// Gracefully closes the given socket. Closes the underlying |zx::socket| as
// well, even if the socket provider returns an error.
//
// Returns the |zx_status_t| from the socket provider (rather than from the
// kernel when closing the underlying |zx_handle_t|).
zx_status_t zxs_close(zxs_socket_t socket);

// Send the data in the given |buffer| over |socket|.
zx_status_t zxs_send(const zxs_socket_t* socket, const void* buffer, size_t capacity,
                     size_t* out_actual);

// Receive data from |socket| into the given |buffer|.
zx_status_t zxs_recv(const zxs_socket_t* socket, void* buffer, size_t capacity, size_t* out_actual);

// Send the data in the given |buffer| to |addr| over |socket|.
zx_status_t zxs_sendto(const zxs_socket_t* socket, const struct sockaddr* addr, size_t addr_length,
                       const void* buffer, size_t capacity, size_t* out_actual);

// Receive data from |socket| into the given |buffer|.
//
// The |addr| buffer is filled with the address from which the data was
// received.
zx_status_t zxs_recvfrom(const zxs_socket_t* socket, struct sockaddr* addr, size_t addr_capacity,
                         size_t* out_addr_actual, void* buffer, size_t capacity,
                         size_t* out_actual);

// Send the data described by |msg| over the given |socket|.
//
// The |out_actual| parameter is the amount of data sent by this call, gathered
// from the |iovec| records referenced by |msg|.
zx_status_t zxs_sendmsg(const zxs_socket_t* socket, const struct msghdr* msg, size_t* out_actual);

// Receive data from |socket| into the buffers described by |msg|.
//
// The |out_actual| parameter is the amount of data received by this call,
// scattered to the |iovec| records referenced by |msg|.
zx_status_t zxs_recvmsg(const zxs_socket_t* socket, struct msghdr* msg, size_t* out_actual);

__END_CDECLS

#endif  // LIB_ZXS_ZXS_H_

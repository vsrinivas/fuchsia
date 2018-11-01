// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXS_ZXS_H_
#define LIB_ZXS_ZXS_H_

#include <fuchsia/net/c/fidl.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Flags that describe how the |zxs| library will interact with the kernel
// socket object.
typedef uint32_t zxs_flags_t;

// If set, operations wait for the remote party to provide the necessary data or
// objects to complete the operation.
#define ZXS_FLAG_BLOCKING ((zxs_flags_t)1u << 0)

// If set, the socket is used to transport data in atomic chunks.
#define ZXS_FLAG_DATAGRAM ((zxs_flags_t)1u << 1)

// A socket.
typedef struct zxs_socket {
    // The underlying socket kernel object.
    zx_handle_t socket;

    // Flags that describe how the |zxs| library will interact with the kernel
    // socket object.
    zxs_flags_t flags;
} zxs_socket_t;

// An option for a |zxs_socket_t|.
typedef struct zxs_option {
    // See POSIX documentation for the available levels.
    //
    // For example, |man 7 socket|.
    //
    // TODO: Document the levels we support.
    int32_t level;

    // See POSIX documentation for the available option names.
    //
    // For example, |man 7 socket|.
    //
    // TODO: Document the names we support.
    int32_t name;

    // A pointer to the value of the option.
    const void* value;

    // The number of bytes pointed to by |value|.
    size_t length;
} zxs_option_t;

// Create a |zxs_socket_t|.
//
// Given a channel |socket_provider| that implements the
// |fuchsia.net.LegacySocketProvider| interface, create a |zxs_socket_t| with
// the given characteristics.
//
// This function does not take ownership of |socket_provider|.
zx_status_t zxs_socket(zx_handle_t socket_provider,
                       fuchsia_net_SocketDomain domain,
                       fuchsia_net_SocketType type,
                       fuchsia_net_SocketProtocol protocol,
                       const zxs_option_t* options,
                       size_t options_count,
                       zxs_socket_t* out_socket);

// Connect the given |socket| to the given |addr|.
//
// If |socket| is a datagram socket, then |addr| is the address to which
// datagrams are sent by default and the only address from which datagrams are
// received.
//
// If |socket| is a stream socket, then this function attempts to establish a
// connection to |addr|.
zx_status_t zxs_connect(const zxs_socket_t* socket, const struct sockaddr* addr,
                        size_t addr_length);

// Assign a name to |socket|.
//
// Typically, stream sockets will require a local name to be assigned before
// they can receive connections.
zx_status_t zxs_bind(const zxs_socket_t* socket, const struct sockaddr* addr,
                     size_t addr_length);

// Mark |socket| as ready to accept connections.
//
// The |backlog| parameter is a hint for the depth of the queue of unaccepted
// connections. A |backlog| of zero indicates that no hint is provided.
zx_status_t zxs_listen(const zxs_socket_t* socket, uint32_t backlog);

// Extract a |zxs_socket_t| from the queue of unaccepted sockets.
//
// The |socket| must first have been marked as ready to accept connections using
// |zxs_listen|.
//
// The |addr| buffer is filled with the address of peer socket.
zx_status_t zxs_accept(const zxs_socket_t* socket, struct sockaddr* addr,
                       size_t addr_capacity, size_t* out_addr_actual,
                       zxs_socket_t* out_socket);

// Get the current address to which |socket| is bound.
//
// See |zxs_bind|.
zx_status_t zxs_getsockname(const zxs_socket_t* socket, struct sockaddr* addr,
                            size_t capacity, size_t* out_actual);

// Get the address of the peer for |socket|.
zx_status_t zxs_getpeername(const zxs_socket_t* socket, struct sockaddr* addr,
                            size_t capacity, size_t* out_actual);

// Get the socket option with the given |level| and |name|.
zx_status_t zxs_getsockopt(const zxs_socket_t* socket, int32_t level,
                           int32_t name, void* buffer, size_t capacity,
                           size_t* out_actual);

// Set the given |options| on |socket|.
//
// The |count| parameter is the number of |zxs_option_t| records pointed to by
// |options|.
zx_status_t zxs_setsockopts(const zxs_socket_t* socket,
                            const zxs_option_t* options,
                            size_t count);

// Send the data in the given |buffer| over |socket|.
zx_status_t zxs_send(const zxs_socket_t* socket, const void* buffer,
                     size_t capacity, size_t* out_actual);

// Receive data from |socket| into the given |buffer|.
zx_status_t zxs_recv(const zxs_socket_t* socket, int flag, void* buffer,
                     size_t capacity, size_t* out_actual);

// Send the data in the given |buffer| to |addr| over |socket|.
zx_status_t zxs_sendto(const zxs_socket_t* socket, const struct sockaddr* addr,
                       size_t addr_length, const void* buffer, size_t capacity,
                       size_t* out_actual);

// Receive data from |socket| into the given |buffer|.
//
// The |addr| buffer is filled with the address from which the data was
// received.
zx_status_t zxs_recvfrom(const zxs_socket_t* socket, struct sockaddr* addr,
                        size_t addr_capacity, size_t* out_addr_actual,
                        void* buffer, size_t capacity, size_t* out_actual);

// Send the data described by |msg| over the given |socket|.
//
// The |out_actual| parameter is the amount of data sent by this call, gathered
// from the |iovec| records referenced by |msg|.
zx_status_t zxs_sendmsg(const zxs_socket_t* socket, const struct msghdr* msg,
                        size_t* out_actual);

// Receive data from |socket| into the buffers described by |msg|.
//
// The |out_actual| parameter is the amount of data received by this call,
// scattered to the |iovec| records referenced by |msg|.
zx_status_t zxs_recvmsg(const zxs_socket_t* socket, struct msghdr* msg,
                        size_t* out_actual);

__END_CDECLS

#endif // LIB_ZXS_ZXS_H_

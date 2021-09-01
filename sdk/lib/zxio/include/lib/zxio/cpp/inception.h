// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/zx/debuglog.h>
#include <lib/zxio/ops.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// This header exposes some guts of zxio in order to transition fdio to build on
// top of zxio.

// pipe ------------------------------------------------------------------------

// A |zxio_t| backend that uses a Zircon socket object.
//
// The |socket| handle is a Zircon socket object.
//
// Will eventually be an implementation detail of zxio once fdio completes its
// transition to the zxio backend.
using zxio_pipe_t = struct zxio_pipe {
  zxio_t io;
  zx::socket socket;
};

static_assert(sizeof(zxio_pipe_t) <= sizeof(zxio_storage_t),
              "zxio_pipe_t must fit inside zxio_storage_t.");

// datagram socket (channel backed) --------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.DatagramSocket object.
using zxio_datagram_socket_t = struct zxio_datagram_socket {
  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<fuchsia_posix_socket::DatagramSocket> client;
};

// stream socket (channel backed) --------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.StreamSocket object.
using zxio_stream_socket_t = struct zxio_stream_socket {
  zxio_t io;

  zxio_pipe_t pipe;

  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
};

// raw socket (channel backed) -------------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.raw.Socket object.
using zxio_raw_socket_t = struct zxio_raw_socket {
  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<fuchsia_posix_socket_raw::Socket> client;
};

zx_status_t zxio_is_socket(zxio_t* io, bool* out_is_socket);

// Allocates storage for a zxio_t object of a given type.
//
// This function should store a pointer to zxio_storage_t space suitable for an
// object of the given type into |*out_storage| and return ZX_OK.
// If the allocation fails, this should store the null value into |*out_storage|
// and return an error value. Returning a status other than ZX_OK or failing to store
// a non-null value into |*out_storage| are considered allocation failures.
//
// This function may also store additional data related to the allocation in
// |*out_context| which will be returned in functions that use this allocator.
// This can be useful if the allocator is allocating zxio_storage_t within a
// larger allocation to keep track of that allocation.
using zxio_storage_alloc = zx_status_t (*)(zxio_object_type_t type, zxio_storage_t** out_storage,
                                           void** out_context);

// Creates a new zxio_t object wrapping |handle| into storage provided by the specified
// allocation function |allocator|.
//
// On success, returns ZX_OK and initializes a zxio_t instance into the storage provided by the
// allocator. This also stores the context provided by the allocator into |*out_context|.
//
// If |allocator| returns an error or fails to allocate storage, returns
// ZX_ERR_NO_MEMORY and consumes |handle|. The allocator's error value is not
// preserved. The allocator may store additional context into |*out_context| on
// errors if needed.
//
// See zxio_create() for other error values and postconditions.
zx_status_t zxio_create_with_allocator(zx::handle handle, zxio_storage_alloc allocator,
                                       void** out_context);

// Like zxio_create_with_allocator but the caller supplies handle info for the
// handle.
zx_status_t zxio_create_with_allocator(zx::handle handle, const zx_info_handle_basic_t& handle_info,
                                       zxio_storage_alloc allocator, void** out_context);

// Like zxio_create_with_allocator but the caller supplies information about
// |channel| provided by the server through a Describe call or OnOpen event.
//
// Always consumes |node|. May mutate |info| on success.
zx_status_t zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node> node,
                                       fuchsia_io::wire::NodeInfo& info,
                                       zxio_storage_alloc allocator, void** out_context);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

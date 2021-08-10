// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <fuchsia/posix/socket/raw/llcpp/fidl.h>
#include <lib/zx/debuglog.h>
#include <lib/zxio/ops.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// This header exposes some guts of zxio in order to transition fdio to build on
// top of zxio.

// remote ----------------------------------------------------------------------

// A |zxio_t| backend that uses the |fuchsia.io.Node| protocol.
//
// The |control| handle is a channel that implements the |fuchsia.io.Node|. The
// |event| handle is an optional event object used with some |fuchsia.io.Node|
// servers.
//
// Will eventually be an implementation detail of zxio once fdio completes its
// transition to the zxio backend.
using zxio_remote_t = struct zxio_remote {
  zxio_t io;
  zx_handle_t control;
  zx_handle_t event;
  zx_handle_t stream;
};

static_assert(sizeof(zxio_remote_t) <= sizeof(zxio_storage_t),
              "zxio_remote_t must fit inside zxio_storage_t.");

zx_status_t zxio_remote_init(zxio_storage_t* remote, zx_handle_t control, zx_handle_t event);

// vmo -------------------------------------------------------------------------

// Initialize |file| with from a VMO.
//
// The file will be sized to match the underlying VMO by reading the size of the
// VMO from the kernel. The size of a VMO is always a multiple of the page size,
// which means the size of the file will also be a multiple of the page size.
//
// The |offset| is the initial seek offset within the file.
zx_status_t zxio_vmo_init(zxio_storage_t* file, zx::vmo vmo, zx::stream stream);

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

zx_status_t zxio_pipe_init(zxio_storage_t* pipe, zx::socket socket, zx_info_socket_t info);

// datagram socket (channel backed) --------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.DatagramSocket object.
using zxio_datagram_socket_t = struct zxio_datagram_socket {
  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<fuchsia_posix_socket::DatagramSocket> client;
};

zx_status_t zxio_datagram_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                      fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client);

// stream socket (channel backed) ----------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.StreamSocket object.
using zxio_stream_socket_t = struct zxio_stream_socket {
  zxio_t io;

  zxio_pipe_t pipe;

  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
};

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client,
                                    zx_info_socket_t& info);

// raw socket (channel backed) -------------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.raw.Socket object.
using zxio_raw_socket_t = struct zxio_raw_socket {
  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<fuchsia_posix_socket_raw::Socket> client;
};

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client);

zx_status_t zxio_is_socket(zxio_t* io, bool* out_is_socket);

// generic  --------------------------------------------------------------------

using zxio_object_type_t = uint32_t;

// clang-format off
#define ZXIO_OBJECT_TYPE_NONE            ((zxio_object_type_t) 0)
#define ZXIO_OBJECT_TYPE_DIR             ((zxio_object_type_t) 1)
#define ZXIO_OBJECT_TYPE_SERVICE         ((zxio_object_type_t) 2)
#define ZXIO_OBJECT_TYPE_FILE            ((zxio_object_type_t) 3)
#define ZXIO_OBJECT_TYPE_DEVICE          ((zxio_object_type_t) 4)
#define ZXIO_OBJECT_TYPE_TTY             ((zxio_object_type_t) 5)
#define ZXIO_OBJECT_TYPE_VMOFILE         ((zxio_object_type_t) 6)
#define ZXIO_OBJECT_TYPE_VMO             ((zxio_object_type_t) 7)
#define ZXIO_OBJECT_TYPE_DEBUGLOG        ((zxio_object_type_t) 8)
#define ZXIO_OBJECT_TYPE_PIPE            ((zxio_object_type_t) 9)
#define ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET ((zxio_object_type_t)10)
#define ZXIO_OBJECT_TYPE_STREAM_SOCKET   ((zxio_object_type_t)11)
#define ZXIO_OBJECT_TYPE_RAW_SOCKET      ((zxio_object_type_t)12)
// clang-format on

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

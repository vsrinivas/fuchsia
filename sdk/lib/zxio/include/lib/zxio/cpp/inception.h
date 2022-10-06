// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/types.h>
#include <sys/socket.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

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

// synchronous datagram socket (channel backed) --------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.SynchronousDatagramSocket object.
using zxio_synchronous_datagram_socket_t = struct zxio_synchronous_datagram_socket {
  using FidlProtocol = fuchsia_posix_socket::SynchronousDatagramSocket;

  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<FidlProtocol> client;
};

static_assert(sizeof(zxio_synchronous_datagram_socket_t) <= sizeof(zxio_storage_t),
              "zxio_synchronous_datagram_socket_t must fit inside zxio_storage_t.");

// datagram socket (channel backed) --------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.DatagramSocket object.
using zxio_datagram_socket_t = struct zxio_datagram_socket {
  zxio_t io;
  zxio_pipe_t pipe;
  const zxio_datagram_prelude_size_t prelude_size;
  RouteCache route_cache;
  RequestedCmsgCache cmsg_cache;
  fidl::WireSyncClient<fuchsia_posix_socket::DatagramSocket> client;
};

static_assert(sizeof(zxio_datagram_socket_t) <= sizeof(zxio_storage_t),
              "zxio_datagram_socket_t must fit inside zxio_storage_t.");

// stream socket (channel backed) --------------------------------------------

enum class zxio_stream_socket_state_t {
  UNCONNECTED,
  LISTENING,
  CONNECTING,
  CONNECTED,
};

// A |zxio_t| backend that uses a fuchsia.posix.socket.StreamSocket object.
using zxio_stream_socket_t = struct zxio_stream_socket {
  zxio_t io;
  zxio_pipe_t pipe;
  std::mutex state_lock;
  zxio_stream_socket_state_t state __TA_GUARDED(state_lock);
  fidl::WireSyncClient<fuchsia_posix_socket::StreamSocket> client;
};

static_assert(sizeof(zxio_stream_socket_t) <= sizeof(zxio_storage_t),
              "zxio_stream_socket_t must fit inside zxio_storage_t.");

// raw socket (channel backed) -------------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.raw.Socket object.
using zxio_raw_socket_t = struct zxio_raw_socket {
  using FidlProtocol = fuchsia_posix_socket_raw::Socket;

  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<FidlProtocol> client;
};

static_assert(sizeof(zxio_raw_socket_t) <= sizeof(zxio_storage_t),
              "zxio_raw_socket_t must fit inside zxio_storage_t.");

// packet socket (channel backed) ----------------------------------------------

// A |zxio_t| backend that uses a fuchsia.posix.socket.packet.Socket object.
using zxio_packet_socket_t = struct zxio_packet_socket {
  using FidlProtocol = fuchsia_posix_socket_packet::Socket;

  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<FidlProtocol> client;
};

static_assert(sizeof(zxio_packet_socket_t) <= sizeof(zxio_storage_t),
              "zxio_packet_socket_t must fit inside zxio_storage_t.");

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

// Like zxio_create_with_allocator but the caller supplies information about
// |channel| provided by the server through a Describe call or OnOpen event.
//
// Always consumes |node|. May mutate |info| on success.
zx_status_t zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node> node,
                                       fuchsia_io::wire::NodeInfoDeprecated& info,
                                       zxio_storage_alloc allocator, void** out_context);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_INCEPTION_H_

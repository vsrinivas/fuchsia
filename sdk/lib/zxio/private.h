// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_PRIVATE_H_
#define LIB_ZXIO_PRIVATE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zxio/cpp/dgram_cache.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <algorithm>
#include <functional>

template <typename F>
zx_status_t zxio_vmo_do_vector(size_t start, size_t length, size_t* offset,
                               const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                               F fn) {
  if (*offset > length) {
    return ZX_ERR_INVALID_ARGS;
  }
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* buffer, size_t capacity, size_t* out_actual) {
                          capacity = std::min(capacity, length - *offset);
                          zx_status_t status = fn(buffer, start + *offset, capacity);
                          if (status != ZX_OK) {
                            return status;
                          }
                          *offset += capacity;
                          *out_actual = capacity;
                          return ZX_OK;
                        });
}

// A utility which helps implementing the C-style |zxio_ops_t| ops table
// from a C++ class. The specific backend implementation should inherit
// from |HasIo| as the first base class, ensuring that the |zxio_t| part
// appears that the beginning of its object layout.
class HasIo {
 protected:
  explicit HasIo(const zxio_ops_t& ops) { zxio_init(&io_, &ops); }

  zxio_t* io() { return &io_; }
  const zxio_t* io() const { return &io_; }

  template <typename T>
  struct Adaptor {
    static_assert(std::is_base_of<HasIo, T>::value);
    static_assert(sizeof(T) <= sizeof(zxio_storage_t),
                  "C++ implementation class must fit inside zxio_storage_t.");
    static_assert(!std::is_polymorphic_v<T>,
                  "C++ implementation class must be not contain vtables.");

    // Converts a member function in the implementation C++ class to a signature
    // compatible with the definition in the ops table.
    //
    // This class assumes the |zxio_t*| pointer as passed as the first argument to
    // all |zxio_ops_t| entries is the pointer to the C++ implementation instance.
    //
    // For example, given the |release| call with the following signature:
    //
    //   zx_status_t (*release)(zxio_t* io, zx_handle_t* out_handle);
    //
    // The C++ implementation may define a member function with this signature:
    //
    //   zx_status_t MyClass::Release(zx_handle_t* out_handle);
    //
    // And |Adaptor<MyClass>::From<&Release>| will evaluate to a function with a
    // signature compatible to the C-style definition, treating |io| as a pointer
    // to the |HasIo|, invoking the corresponding member function automatically.
    template <auto fn, typename... Args>
    static zx_status_t From(Args... args) {
      auto memfn = std::mem_fn(fn);
      return FromImpl(memfn, args...);
    }

   private:
    template <typename MemFn, typename... Args>
    static zx_status_t FromImpl(MemFn memfn, zxio_t* io, Args... args) {
      T* instance = reinterpret_cast<T*>(io);
      return memfn(instance, args...);
    }
  };

 private:
  static constexpr void CheckLayout();

  zxio_t io_;
};

constexpr void HasIo::CheckLayout() {
  static_assert(offsetof(HasIo, io_) == 0);
  static_assert(alignof(HasIo) == alignof(zxio_t));
}

uint32_t zxio_node_protocols_to_posix_type(zxio_node_protocols_t protocols);

bool zxio_is_valid(const zxio_t* io);

zx_status_t zxio_dir_init(zxio_storage_t* storage, fidl::ClientEnd<fuchsia_io::Node> client);

zx_status_t zxio_file_init(zxio_storage_t* storage, zx::event event, zx::stream stream,
                           fidl::ClientEnd<fuchsia_io::Node> client);

zx_status_t zxio_pipe_init(zxio_storage_t* pipe, zx::socket socket, zx_info_socket_t info);

// debuglog --------------------------------------------------------------------

// Initializes a |zxio_storage_t| to use the given |handle| for output.
//
// The |handle| should be a Zircon debuglog object.
zx_status_t zxio_debuglog_init(zxio_storage_t* storage, zx::debuglog handle);

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

zx_status_t zxio_synchronous_datagram_socket_init(
    zxio_storage_t* storage, zx::eventpair event,
    fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket> client);

// datagram socket (channel backed)

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

zx_status_t zxio_datagram_socket_init(zxio_storage_t* storage, zx::socket socket,
                                      const zx_info_socket_t& info,
                                      const zxio_datagram_prelude_size_t& prelude_size,
                                      fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client);

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

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    const zx_info_socket_t& info, bool is_connected,
                                    fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client);

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

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client);

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

zx_status_t zxio_packet_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                    fidl::ClientEnd<fuchsia_posix_socket_packet::Socket> client);

// remote ----------------------------------------------------------------------

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx::event event,
                             fidl::ClientEnd<fuchsia_io::Node> client, bool is_tty);
zx_status_t zxio_remote_init(zxio_storage_t* storage, zx::eventpair event,
                             fidl::ClientEnd<fuchsia_io::Node> client, bool is_tty);

// vmo -------------------------------------------------------------------------

// Initialize |file| with from a VMO.
//
// The file will be sized to match the underlying VMO by reading the size of the
// VMO from the kernel. The size of a VMO is always a multiple of the page size,
// which means the size of the file will also be a multiple of the page size.
zx_status_t zxio_vmo_init(zxio_storage_t* file, zx::vmo vmo, zx::stream stream);

zx_status_t zxio_create_with_nodeinfo(fidl::ClientEnd<fuchsia_io::Node> node,
                                      fuchsia_io::wire::NodeInfoDeprecated& node_info,
                                      zxio_storage_t* storage);

zx::status<fuchsia_io::wire::NodeInfoDeprecated> zxio_get_nodeinfo(
    fidl::AnyArena& alloc, const fidl::ClientEnd<fuchsia_io::Node>& node);

#endif  // LIB_ZXIO_PRIVATE_H_

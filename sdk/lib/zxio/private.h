// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_PRIVATE_H_
#define LIB_ZXIO_PRIVATE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <fuchsia/posix/socket/raw/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/extensions.h>
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

void zxio_node_init(zxio_node_t* node, zx_handle_t control, const zxio_extension_ops_t* ops);

zx_status_t zxio_dir_init(zxio_storage_t* remote, zx_handle_t control);

zx_status_t zxio_file_init(zxio_storage_t* remote, zx_handle_t control, zx_handle_t event,
                           zx_handle_t stream);

zx_status_t zxio_pipe_init(zxio_storage_t* pipe, zx::socket socket, zx_info_socket_t info);

// debuglog --------------------------------------------------------------------

// Initializes a |zxio_storage_t| to use the given |handle| for output.
//
// The |handle| should be a Zircon debuglog object.
zx_status_t zxio_debuglog_init(zxio_storage_t* storage, zx::debuglog handle);

// datagram socket (channel backed) --------------------------------------------

zx_status_t zxio_datagram_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                      fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client);

// stream socket (channel backed) --------------------------------------------

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client,
                                    zx_info_socket_t& info);

// raw socket (channel backed) --------------------------------------------

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client);

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

// remote v2 -------------------------------------------------------------------

// A |zxio_t| backend that uses the |fuchsia.io2/Node| protocol.
//
// The |control| handle is a channel that implements the |fuchsia.io2/Node|. The
// |observer| handle is an optional object used with some |fuchsia.io2/Node|
// servers. The |stream| handle is an optional stream object associated with the
// file. See fuchsia.io2/FileInfo for additional documentation.
//
// Will eventually be an implementation detail of zxio once fdio completes its
// transition to the zxio backend.
using zxio_remote_v2_t = struct zxio_remote_v2 {
  zxio_t io;
  zx_handle_t control;
  zx_handle_t observer;
  zx_handle_t stream;
};

static_assert(sizeof(zxio_remote_v2_t) <= sizeof(zxio_storage_t),
              "zxio_remote_v2_t must fit inside zxio_storage_t.");

zx_status_t zxio_remote_v2_init(zxio_storage_t* remote, zx_handle_t control, zx_handle_t observer);
zx_status_t zxio_dir_v2_init(zxio_storage_t* remote, zx_handle_t control);
zx_status_t zxio_file_v2_init(zxio_storage_t* remote, zx_handle_t control, zx_handle_t observer,
                              zx_handle_t stream);

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

// vmofile ---------------------------------------------------------------------

zx_status_t zxio_vmofile_init(zxio_storage_t* file, fidl::WireSyncClient<fuchsia_io::File> control,
                              zx::vmo vmo, zx_off_t offset, zx_off_t length, zx_off_t seek);

zx_status_t zxio_vmo_get_common(const zx::vmo& vmo, size_t content_size, uint32_t flags,
                                zx_handle_t* out_vmo, size_t* out_size);

zx_status_t zxio_create_with_nodeinfo(fidl::ClientEnd<fuchsia_io::Node> node,
                                      fuchsia_io::wire::NodeInfo& node_info,
                                      zxio_storage_t* storage);

// Common functionalities shared by the fuchsia.io v1 |node| and |remote| transports.
// These operate on the raw FIDL channel directly, as |node| and |remote|
// have different object layouts.

// Send a |fuchsia.io/Node.Close| message on |control|. Note: does not close the channel.
zx_status_t zxio_raw_remote_close(zx::unowned_channel control);

zx_status_t zxio_raw_remote_clone(zx::unowned_channel source, zx_handle_t* out_handle);

zx_status_t zxio_raw_remote_attr_get(zx::unowned_channel control, zxio_node_attributes_t* out_attr);

zx_status_t zxio_raw_remote_attr_set(zx::unowned_channel control,
                                     const zxio_node_attributes_t* attr);

#endif  // LIB_ZXIO_PRIVATE_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/posix/socket/llcpp/fidl.h>
#include <fuchsia/posix/socket/raw/llcpp/fidl.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <zircon/syscalls/object.h>

namespace zxio {

inline zx_status_t CreateDatagramSocket(
    zxio_storage_t* storage, zx::eventpair event,
    fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET, event.release(),
                               client.TakeChannel().release());
}

inline zx_status_t CreateDirectory(zxio_storage_t* storage,
                                   fidl::ClientEnd<fuchsia_io::Directory> directory) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_DIR, directory.TakeChannel().release());
}

inline zx_status_t CreateNode(zxio_storage_t* storage, fidl::ClientEnd<fuchsia_io::Node> node) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_NODE, node.TakeChannel().release());
}

inline zx_status_t CreateStreamSocket(zxio_storage_t* storage, zx::socket socket,
                                      fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client,
                                      zx_info_socket_t& info) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_STREAM_SOCKET, socket.release(),
                               client.TakeChannel().release(), &info);
}

inline zx_status_t CreatePipe(zxio_storage_t* storage, zx::socket socket, zx_info_socket_t& info) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_PIPE, socket.release(), &info);
}

inline zx_status_t CreateRawSocket(zxio_storage_t* storage, zx::eventpair event,
                                   fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_RAW_SOCKET, event.release(),
                               client.TakeChannel().release());
}

inline zx_status_t CreateVmo(zxio_storage_t* storage, zx::vmo vmo, zx::stream stream) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_VMO, vmo.release(), stream.release());
}

}  // namespace zxio

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_

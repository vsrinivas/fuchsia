// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/zxio.h>
#include <zircon/syscalls/object.h>

namespace zxio {

inline zx_status_t CreateDirectory(zxio_storage_t* storage,
                                   fidl::ClientEnd<fuchsia_io::Directory> directory) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_DIR, directory.TakeChannel().release());
}

inline zx_status_t CreateNode(zxio_storage_t* storage, fidl::ClientEnd<fuchsia_io::Node> node) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_NODE, node.TakeChannel().release());
}

inline zx_status_t CreatePipe(zxio_storage_t* storage, zx::socket socket, zx_info_socket_t& info) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_PIPE, socket.release(), &info);
}

inline zx_status_t CreateVmo(zxio_storage_t* storage, zx::vmo vmo, zx::stream stream) {
  return zxio_create_with_type(storage, ZXIO_OBJECT_TYPE_VMO, vmo.release(), stream.release());
}

}  // namespace zxio

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_CREATE_WITH_TYPE_H_

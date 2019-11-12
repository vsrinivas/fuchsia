// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/internal/node_connection.h>

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/handle.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/assert.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <fbl/string_buffer.h>
#include <fs/debug.h>
#include <fs/handler.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

zx_status_t NodeConnection::HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  zx_status_t status = fuchsia_io_Node_try_dispatch(this, txn, msg, &kOps);
  if (status != ZX_ERR_NOT_SUPPORTED) {
    return status;
  }
  return vnode()->HandleFsSpecificMessage(msg, txn);
}

zx_status_t NodeConnection::Clone(uint32_t clone_flags, zx_handle_t object) {
  return Connection::NodeClone(clone_flags, object);
}

zx_status_t NodeConnection::Close(fidl_txn_t* txn) { return Connection::NodeClose(txn); }

zx_status_t NodeConnection::Describe(fidl_txn_t* txn) { return Connection::NodeDescribe(txn); }

zx_status_t NodeConnection::Sync(fidl_txn_t* txn) { return Connection::NodeSync(txn); }

zx_status_t NodeConnection::GetAttr(fidl_txn_t* txn) { return Connection::NodeGetAttr(txn); }

zx_status_t NodeConnection::SetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                                    fidl_txn_t* txn) {
  return Connection::NodeSetAttr(flags, attributes, txn);
}

zx_status_t NodeConnection::NodeGetFlags(fidl_txn_t* txn) {
  return Connection::NodeNodeGetFlags(txn);
}

zx_status_t NodeConnection::NodeSetFlags(uint32_t flags, fidl_txn_t* txn) {
  return Connection::NodeNodeSetFlags(flags, txn);
}

}  // namespace internal

}  // namespace fs

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/extensions.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include "private.h"

struct zxio_node_internal_t {
  zxio_t io;
  const zxio_extension_ops_t* extension_ops;
  zx_handle_t control;
};

static_assert(sizeof(zxio_node_internal_t) == sizeof(zxio_node_t),
              "Layouts of zxio_node_internal_t and zxio_node_t should match");
static_assert(sizeof(zxio_node_internal_t) <= sizeof(zxio_storage_t),
              "zxio_node_internal_t should fit in the general storage");

namespace {

zxio_node_internal_t* to_internal(zxio_node_t* io) {
  return reinterpret_cast<zxio_node_internal_t*>(io);
}

zxio_node_internal_t* to_internal(zxio_t* io) {
  return reinterpret_cast<zxio_node_internal_t*>(io);
}

const zxio_node_internal_t* to_internal(const zxio_node_t* io) {
  return reinterpret_cast<const zxio_node_internal_t*>(io);
}

zxio_node_t* to_node(zxio_t* io) { return reinterpret_cast<zxio_node_t*>(io); }

zx_status_t zxio_node_close(zxio_t* io) {
  zxio_node_internal_t* node = to_internal(io);
  if (node->extension_ops && node->extension_ops->close) {
    node->extension_ops->close(to_node(io));
  }
  zx_status_t status = ZX_OK;
  if (!node->extension_ops || !node->extension_ops->skip_close_call) {
    status = zxio_raw_remote_close(zx::unowned_channel(node->control));
  }
  zx_handle_close(node->control);
  node->control = ZX_HANDLE_INVALID;
  return status;
}

zx_status_t zxio_node_release(zxio_t* io, zx_handle_t* out_handle) {
  zxio_node_internal_t* node = to_internal(io);
  *out_handle = node->control;
  node->control = ZX_HANDLE_INVALID;
  return ZX_OK;
}

zx_status_t zxio_node_clone(zxio_t* io, zx_handle_t* out_handle) {
  return zxio_raw_remote_clone(zx::unowned_channel(to_internal(io)->control), out_handle);
}

zx_status_t zxio_node_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  return zxio_raw_remote_attr_get(zx::unowned_channel(to_internal(io)->control), out_attr);
}

zx_status_t zxio_node_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  return zxio_raw_remote_attr_set(zx::unowned_channel(to_internal(io)->control), attr);
}

zx_status_t zxio_node_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                            zxio_flags_t flags, size_t* out_actual) {
  zxio_node_internal_t* node = to_internal(io);
  if (!node->extension_ops || !node->extension_ops->readv) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return node->extension_ops->readv(to_node(io), vector, vector_count, flags, out_actual);
}

zx_status_t zxio_node_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                             zxio_flags_t flags, size_t* out_actual) {
  zxio_node_internal_t* node = to_internal(io);
  if (!node->extension_ops || !node->extension_ops->writev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return node->extension_ops->writev(to_node(io), vector, vector_count, flags, out_actual);
}

}  // namespace

static constexpr zxio_ops_t zxio_node_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_node_close;
  ops.release = zxio_node_release;
  ops.clone = zxio_node_clone;
  ops.attr_get = zxio_node_attr_get;
  ops.attr_set = zxio_node_attr_set;
  ops.readv = zxio_node_readv;
  ops.writev = zxio_node_writev;
  return ops;
}();

void zxio_node_init(zxio_node_t* node, zx_handle_t control, const zxio_extension_ops_t* ops) {
  zxio_node_internal_t* remote = to_internal(node);
  zxio_init(&remote->io, &zxio_node_ops);
  remote->control = control;
  remote->extension_ops = ops;
}

zx_handle_t zxio_node_borrow_channel(const zxio_node_t* node) { return to_internal(node)->control; }

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include <type_traits>

#include "private.h"

namespace fio2 = llcpp::fuchsia::io2;

namespace {

// C++ wrapper around zxio_remote_v2_t.
class Remote {
 public:
  explicit Remote(zxio_t* io) : rio_(reinterpret_cast<zxio_remote_v2_t*>(io)) {}

  [[nodiscard]] zx::unowned_channel control() const { return zx::unowned_channel(rio_->control); }

  [[nodiscard]] zx::unowned_handle observer() const { return zx::unowned_handle(rio_->observer); }

  zx::handle Release() {
    zx::handle control(rio_->control);
    rio_->control = ZX_HANDLE_INVALID;
    if (rio_->observer != ZX_HANDLE_INVALID) {
      zx_handle_close(rio_->observer);
      rio_->observer = ZX_HANDLE_INVALID;
    }
    return control;
  }

 private:
  zxio_remote_v2_t* rio_;
};

zxio_node_protocols_t ToZxioNodeProtocols(fio2::NodeProtocolSet protocols) {
  zxio_node_protocols_t zxio_protocols = ZXIO_NODE_PROTOCOL_NONE;
  if (protocols & fio2::NodeProtocolSet::CONNECTOR) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_CONNECTOR;
  }
  if (protocols & fio2::NodeProtocolSet::DIRECTORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DIRECTORY;
  }
  if (protocols & fio2::NodeProtocolSet::FILE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_FILE;
  }
  if (protocols & fio2::NodeProtocolSet::MEMORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_MEMORY;
  }
  if (protocols & fio2::NodeProtocolSet::POSIX_SOCKET) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_POSIX_SOCKET;
  }
  if (protocols & fio2::NodeProtocolSet::PIPE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_PIPE;
  }
  if (protocols & fio2::NodeProtocolSet::DEBUGLOG) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEBUGLOG;
  }
  if (protocols & fio2::NodeProtocolSet::DEVICE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEVICE;
  }
  if (protocols & fio2::NodeProtocolSet::TTY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_TTY;
  }
  return zxio_protocols;
}

fio2::NodeProtocolSet ToIo2NodeProtocols(zxio_node_protocols_t zxio_protocols) {
  fio2::NodeProtocolSet protocols = fio2::NodeProtocolSet();
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    protocols |= fio2::NodeProtocolSet::CONNECTOR;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    protocols |= fio2::NodeProtocolSet::DIRECTORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_FILE) {
    protocols |= fio2::NodeProtocolSet::FILE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_MEMORY) {
    protocols |= fio2::NodeProtocolSet::MEMORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_POSIX_SOCKET) {
    protocols |= fio2::NodeProtocolSet::POSIX_SOCKET;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_PIPE) {
    protocols |= fio2::NodeProtocolSet::PIPE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEBUGLOG) {
    protocols |= fio2::NodeProtocolSet::DEBUGLOG;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEVICE) {
    protocols |= fio2::NodeProtocolSet::DEVICE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_TTY) {
    protocols |= fio2::NodeProtocolSet::TTY;
  }
  return protocols;
}

zxio_abilities_t ToZxioAbilities(fio2::Operations abilities) {
  zxio_abilities_t zxio_abilities = ZXIO_OPERATION_NONE;
  if (abilities & fio2::Operations::CONNECT) {
    zxio_abilities |= ZXIO_OPERATION_CONNECT;
  }
  if (abilities & fio2::Operations::READ_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_READ_BYTES;
  }
  if (abilities & fio2::Operations::WRITE_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_WRITE_BYTES;
  }
  if (abilities & fio2::Operations::EXECUTE) {
    zxio_abilities |= ZXIO_OPERATION_EXECUTE;
  }
  if (abilities & fio2::Operations::GET_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
  }
  if (abilities & fio2::Operations::UPDATE_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
  }
  if (abilities & fio2::Operations::ENUMERATE) {
    zxio_abilities |= ZXIO_OPERATION_ENUMERATE;
  }
  if (abilities & fio2::Operations::TRAVERSE) {
    zxio_abilities |= ZXIO_OPERATION_TRAVERSE;
  }
  if (abilities & fio2::Operations::MODIFY_DIRECTORY) {
    zxio_abilities |= ZXIO_OPERATION_MODIFY_DIRECTORY;
  }
  if (abilities & fio2::Operations::ADMIN) {
    zxio_abilities |= ZXIO_OPERATION_ADMIN;
  }
  return zxio_abilities;
}

fio2::Operations ToIo2Abilities(zxio_abilities_t zxio_abilities) {
  fio2::Operations abilities = fio2::Operations();
  if (zxio_abilities & ZXIO_OPERATION_CONNECT) {
    abilities |= fio2::Operations::CONNECT;
  }
  if (zxio_abilities & ZXIO_OPERATION_READ_BYTES) {
    abilities |= fio2::Operations::READ_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_WRITE_BYTES) {
    abilities |= fio2::Operations::WRITE_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_EXECUTE) {
    abilities |= fio2::Operations::EXECUTE;
  }
  if (zxio_abilities & ZXIO_OPERATION_GET_ATTRIBUTES) {
    abilities |= fio2::Operations::GET_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_UPDATE_ATTRIBUTES) {
    abilities |= fio2::Operations::UPDATE_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_ENUMERATE) {
    abilities |= fio2::Operations::ENUMERATE;
  }
  if (zxio_abilities & ZXIO_OPERATION_TRAVERSE) {
    abilities |= fio2::Operations::TRAVERSE;
  }
  if (zxio_abilities & ZXIO_OPERATION_MODIFY_DIRECTORY) {
    abilities |= fio2::Operations::MODIFY_DIRECTORY;
  }
  if (zxio_abilities & ZXIO_OPERATION_ADMIN) {
    abilities |= fio2::Operations::ADMIN;
  }
  return abilities;
}

zxio_node_attr_t ToZxioNodeAttr(const fio2::NodeAttributes& attr) {
  zxio_node_attr_t zxio_attr = {};
  if (attr.has_protocols()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, protocols, ToZxioNodeProtocols(attr.protocols()));
  }
  if (attr.has_abilities()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, protocols, ToZxioAbilities(attr.abilities()));
  }
  if (attr.has_content_size()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, content_size, attr.content_size());
  }
  if (attr.has_storage_size()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, storage_size, attr.storage_size());
  }
  if (attr.has_link_count()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, link_count, attr.link_count());
  }
  if (attr.has_creation_time()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, creation_time, attr.creation_time());
  }
  if (attr.has_modification_time()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, modification_time, attr.modification_time());
  }
  return zxio_attr;
}

// Threading a callback to pass the resulting LLCPP table, since tables
// in LLCPP does not recursively own the data.
template <typename F>
auto ToIo2NodeAttributes(const zxio_node_attr_t& attr, F f)
    -> decltype(f(std::declval<fio2::NodeAttributes>())) {
  auto builder = fio2::NodeAttributes::Build();
  fio2::NodeProtocolSet protocols;
  if (attr.has.protocols) {
    protocols = ToIo2NodeProtocols(attr.protocols);
    builder.set_protocols(&protocols);
  }
  fio2::Operations abilities;
  if (attr.has.abilities) {
    abilities = ToIo2Abilities(attr.abilities);
    builder.set_abilities(&abilities);
  }
  uint64_t content_size;
  if (attr.has.content_size) {
    content_size = attr.content_size;
    builder.set_content_size(&content_size);
  }
  uint64_t storage_size;
  if (attr.has.storage_size) {
    storage_size = attr.storage_size;
    builder.set_storage_size(&storage_size);
  }
  uint64_t link_count;
  if (attr.has.link_count) {
    link_count = attr.link_count;
    builder.set_link_count(&link_count);
  }
  uint64_t creation_time;
  if (attr.has.creation_time) {
    creation_time = attr.creation_time;
    builder.set_creation_time(&creation_time);
  }
  uint64_t modification_time;
  if (attr.has.modification_time) {
    modification_time = attr.modification_time;
    builder.set_modification_time(&modification_time);
  }
  return f(builder.view());
}

// These functions are named with "v2" to avoid mixing up with fuchsia.io v1
// backend during grepping.

zx_status_t zxio_remote_v2_close(zxio_t* io) {
  Remote rio(io);
  auto result = fio2::Node::Call::Close(rio.control());
  // TODO(yifeit): The |Node.Close| method is one-way. In order to catch
  // any server-side error during close, we should wait for an epitaph.
  zx::handle control = rio.Release();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  return control.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
}

zx_status_t zxio_remote_v2_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  Remote rio(io);
  auto result = fio2::Node::Call::GetAttributes(rio.control(), fio2::NodeAttributesQuery::mask);
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->result.is_err()) {
    return result->result.err();
  }
  const fio2::NodeAttributes& attributes = result->result.response().attributes;
  *out_attr = ToZxioNodeAttr(attributes);
  return ZX_OK;
}

zx_status_t zxio_remote_v2_attr_set(zxio_t* io, const zxio_node_attr_t* attr) {
  return ToIo2NodeAttributes(*attr, [io](fio2::NodeAttributes attributes) {
    Remote rio(io);
    auto result = fio2::Node::Call::UpdateAttributes(rio.control(), std::move(attributes));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }
    return ZX_OK;
  });
}

}  // namespace

static constexpr zxio_ops_t zxio_remote_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  return ops;
}();

zx_status_t zxio_remote_v2_init(zxio_storage_t* storage, zx_handle_t control,
                                zx_handle_t observer) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_remote_v2_ops);
  remote->control = control;
  remote->observer = observer;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_dir_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  return ops;
}();

zx_status_t zxio_dir_v2_init(zxio_storage_t* storage, zx_handle_t control) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_dir_v2_ops);
  remote->control = control;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_file_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  return ops;
}();

zx_status_t zxio_file_v2_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t observer) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_file_v2_ops);
  remote->control = control;
  remote->observer = observer;
  return ZX_OK;
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
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
#include <fs/internal/fidl_transaction.h>
#include <fs/internal/node_connection.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

namespace internal {

NodeConnection::NodeConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                               VnodeConnectionOptions options)
    : Connection(vfs, std::move(vnode), protocol, options, FidlProtocol::Create<fio::Node>(this)) {}

void NodeConnection::Clone(uint32_t clone_flags, zx::channel object,
                           CloneCompleter::Sync& completer) {
  Connection::NodeClone(clone_flags, std::move(object));
}

void NodeConnection::Close(CloseCompleter::Sync& completer) {
  auto result = Connection::NodeClose();
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void NodeConnection::Describe(DescribeCompleter::Sync& completer) {
  auto result = Connection::NodeDescribe();
  if (result.is_error()) {
    completer.Close(result.error());
  } else {
    ConvertToIoV1NodeInfo(result.take_value(),
                          [&](fio::NodeInfo&& info) { completer.Reply(std::move(info)); });
  }
}

void NodeConnection::Sync(SyncCompleter::Sync& completer) {
  Connection::NodeSync([completer = completer.ToAsync()](zx_status_t sync_status) mutable {
    completer.Reply(sync_status);
  });
}

void NodeConnection::GetAttr(GetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeGetAttr();
  if (result.is_error()) {
    completer.Reply(result.error(), fio::NodeAttributes());
  } else {
    completer.Reply(ZX_OK, result.value().ToIoV1NodeAttributes());
  }
}

void NodeConnection::SetAttr(uint32_t flags, ::llcpp::fuchsia::io::NodeAttributes attributes,
                             SetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeSetAttr(flags, attributes);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void NodeConnection::NodeGetFlags(NodeGetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeNodeGetFlags();
  if (result.is_error()) {
    completer.Reply(result.error(), 0);
  } else {
    completer.Reply(ZX_OK, result.value());
  }
}

void NodeConnection::NodeSetFlags(uint32_t flags, NodeSetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeNodeSetFlags(flags);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

}  // namespace internal

}  // namespace fs

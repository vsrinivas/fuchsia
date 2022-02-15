// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/node_connection.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
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

#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/fidl_transaction.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {

namespace internal {

NodeConnection::NodeConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                               VnodeProtocol protocol, VnodeConnectionOptions options)
    : Connection(vfs, std::move(vnode), protocol, options, FidlProtocol::Create<fio::Node>(this)) {}

void NodeConnection::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  Connection::NodeClone(request->flags, std::move(request->object));
}

void NodeConnection::CloseDeprecated(CloseDeprecatedRequestView request,
                                     CloseDeprecatedCompleter::Sync& completer) {
  auto result = Connection::NodeClose();
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void NodeConnection::Close(CloseRequestView request, CloseCompleter::Sync& completer) {
  auto result = Connection::NodeClose();
  if (result.is_error()) {
    completer.ReplyError(result.error());
  } else {
    completer.ReplySuccess();
  }
}

void NodeConnection::Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) {
  auto result = Connection::NodeDescribe();
  if (result.is_error()) {
    completer.Close(result.error());
  } else {
    ConvertToIoV1NodeInfo(result.take_value(),
                          [&](fio::wire::NodeInfo&& info) { completer.Reply(std::move(info)); });
  }
}

void NodeConnection::Describe2(Describe2RequestView request, Describe2Completer::Sync& completer) {
  auto result = Connection::NodeDescribe();
  if (result.is_error()) {
    completer.Close(result.error());
    return;
  }
  ConnectionInfoConverter converter(result.take_value());
  completer.Reply(std::move(converter.info));
}

void NodeConnection::SyncDeprecated(SyncDeprecatedRequestView request,
                                    SyncDeprecatedCompleter::Sync& completer) {
  Connection::NodeSync([completer = completer.ToAsync()](zx_status_t sync_status) mutable {
    completer.Reply(sync_status);
  });
}

void NodeConnection::Sync(SyncRequestView request, SyncCompleter::Sync& completer) {
  Connection::NodeSync([completer = completer.ToAsync()](zx_status_t sync_status) mutable {
    if (sync_status != ZX_OK) {
      completer.ReplyError(sync_status);
    } else {
      completer.ReplySuccess();
    }
  });
}

void NodeConnection::GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeGetAttr();
  if (result.is_error()) {
    completer.Reply(result.error(), fio::wire::NodeAttributes());
  } else {
    completer.Reply(ZX_OK, result.value().ToIoV1NodeAttributes());
  }
}

void NodeConnection::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeSetAttr(request->flags, request->attributes);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void NodeConnection::GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeGetFlags();
  if (result.is_error()) {
    completer.Reply(result.error(), 0);
  } else {
    completer.Reply(ZX_OK, result.value());
  }
}

void NodeConnection::SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeSetFlags(request->flags);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void NodeConnection::QueryFilesystem(QueryFilesystemRequestView request,
                                     QueryFilesystemCompleter::Sync& completer) {
  fuchsia_io::wire::FilesystemInfo info;
  zx_status_t status = vnode()->QueryFilesystem(&info);
  completer.Reply(status,
                  status == ZX_OK
                      ? fidl::ObjectView<fuchsia_io::wire::FilesystemInfo>::FromExternal(&info)
                      : nullptr);
}

}  // namespace internal

}  // namespace fs

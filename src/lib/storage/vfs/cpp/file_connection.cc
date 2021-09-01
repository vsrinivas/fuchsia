// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/file_connection.h"

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

#include "src/lib/storage/vfs/cpp/advisory_lock.h"
#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/fidl_transaction.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {

namespace internal {

FileConnection::FileConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                               VnodeProtocol protocol, VnodeConnectionOptions options)
    : Connection(vfs, std::move(vnode), protocol, options, FidlProtocol::Create<fio::File>(this)) {}

void FileConnection::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  Connection::NodeClone(request->flags, std::move(request->object));
}

void FileConnection::Close(CloseRequestView request, CloseCompleter::Sync& completer) {
  auto result = Connection::NodeClose();
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void FileConnection::Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) {
  auto result = Connection::NodeDescribe();
  if (result.is_error()) {
    return completer.Close(result.error());
  }
  ConvertToIoV1NodeInfo(result.take_value(),
                        [&](fio::wire::NodeInfo&& info) { completer.Reply(std::move(info)); });
}

void FileConnection::Sync(SyncRequestView request, SyncCompleter::Sync& completer) {
  Connection::NodeSync([completer = completer.ToAsync()](zx_status_t sync_status) mutable {
    completer.Reply(sync_status);
  });
}

void FileConnection::GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeGetAttr();
  if (result.is_error()) {
    completer.Reply(result.error(), fio::wire::NodeAttributes());
  } else {
    completer.Reply(ZX_OK, result.value().ToIoV1NodeAttributes());
  }
}

void FileConnection::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  auto result = Connection::NodeSetAttr(request->flags, request->attributes);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void FileConnection::NodeGetFlags(NodeGetFlagsRequestView request,
                                  NodeGetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeNodeGetFlags();
  if (result.is_error()) {
    completer.Reply(result.error(), 0);
  } else {
    completer.Reply(ZX_OK, result.value());
  }
}

void FileConnection::NodeSetFlags(NodeSetFlagsRequestView request,
                                  NodeSetFlagsCompleter::Sync& completer) {
  auto result = Connection::NodeNodeSetFlags(request->flags);
  if (result.is_error()) {
    completer.Reply(result.error());
  } else {
    completer.Reply(ZX_OK);
  }
}

void FileConnection::Truncate(TruncateRequestView request, TruncateCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileTruncate] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }
  if (!options().rights.write) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }

  zx_status_t status = vnode()->Truncate(request->length);
  completer.Reply(status);
}

void FileConnection::GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) {
  uint32_t flags = options().ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS);
  completer.Reply(ZX_OK, flags);
}

void FileConnection::SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(request->flags);
  set_append(options.flags.append);
  completer.Reply(ZX_OK);
}

void FileConnection::GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileGetBuffer] our options: ", options(),
                        ", incoming flags: ", ZxFlags(request->flags));

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, nullptr);
  } else if ((request->flags & fio::wire::kVmoFlagPrivate) &&
             (request->flags & fio::wire::kVmoFlagExact)) {
    completer.Reply(ZX_ERR_INVALID_ARGS, nullptr);
  } else if ((options().flags.append) && (request->flags & fio::wire::kVmoFlagWrite)) {
    completer.Reply(ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.write && (request->flags & fio::wire::kVmoFlagWrite)) {
    completer.Reply(ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.execute && (request->flags & fio::wire::kVmoFlagExec)) {
    completer.Reply(ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.read) {
    completer.Reply(ZX_ERR_ACCESS_DENIED, nullptr);
  } else {
    fuchsia_mem::wire::Buffer buffer;
    zx_status_t status = vnode()->GetVmo(request->flags, &buffer.vmo, &buffer.size);
    completer.Reply(status, status == ZX_OK
                                ? fidl::ObjectView<fuchsia_mem::wire::Buffer>::FromExternal(&buffer)
                                : nullptr);
  }
}

void FileConnection::AdvisoryLock(
    fidl::WireServer<fuchsia_io::File>::AdvisoryLockRequestView request,
    AdvisoryLockCompleter::Sync& completer) {
  zx_koid_t owner = GetChannelOwnerKoid();
  // advisory_lock replies to the completer
  auto async_completer = completer.ToAsync();
  fit::callback<void(zx_status_t)> callback = file_lock::lock_completer_t(
      [lock_completer = std::move(async_completer)](zx_status_t status) mutable {
        auto reply = fidl::ObjectView<int32_t>::FromExternal(&status);
        auto result = fuchsia_io2::wire::AdvisoryLockingAdvisoryLockResult::WithErr(reply);
        lock_completer.Reply(std::move(result));
      });

  advisory_lock(owner, vnode(), true, request->request, std::move(callback));
}

void FileConnection::OnTeardown() {
  auto owner = GetChannelOwnerKoid();
  vnode()->DeleteFileLockInTeardown(owner);
}

}  // namespace internal

}  // namespace fs

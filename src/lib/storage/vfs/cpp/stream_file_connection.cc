// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/stream_file_connection.h"

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

StreamFileConnection::StreamFileConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                           zx::stream stream, VnodeProtocol protocol,
                                           VnodeConnectionOptions options)
    : FileConnection(vfs, std::move(vnode), protocol, options), stream_(std::move(stream)) {}

void StreamFileConnection::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (!options().rights.read) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (request->count > fio::wire::kMaxBuf) {
    completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
    return;
  }
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_iovec_t vector = {
      .buffer = data,
      .capacity = request->count,
  };
  zx_status_t status = stream_.readv(0, &vector, 1, &actual);
  ZX_DEBUG_ASSERT(actual <= request->count);
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void StreamFileConnection::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (!options().rights.read) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (request->count > fio::wire::kMaxBuf) {
    completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
    return;
  }
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;

  zx_iovec_t vector = {
      .buffer = data,
      .capacity = request->count,
  };
  zx_status_t status = stream_.readv_at(0, request->offset, &vector, 1, &actual);
  ZX_DEBUG_ASSERT(actual <= request->count);
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void StreamFileConnection::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileWrite] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, 0);
    return;
  }
  if (!options().rights.write) {
    completer.Reply(ZX_ERR_BAD_HANDLE, 0);
    return;
  }
  size_t actual = 0u;
  zx_iovec_t vector = {
      .buffer = request->data.mutable_data(),
      .capacity = request->data.count(),
  };
  uint32_t writev_options = options().flags.append ? ZX_STREAM_APPEND : 0;
  zx_status_t status = stream_.writev(writev_options, &vector, 1, &actual);
  ZX_DEBUG_ASSERT(actual <= request->data.count());
  if (status == ZX_OK) {
    vnode()->DidModifyStream();
  }
  completer.Reply(status, actual);
}

void StreamFileConnection::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileWriteAt] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, 0);
    return;
  }
  if (!options().rights.write) {
    completer.Reply(ZX_ERR_BAD_HANDLE, 0);
    return;
  }
  size_t actual = 0;
  zx_iovec_t vector = {
      .buffer = request->data.mutable_data(),
      .capacity = request->data.count(),
  };
  zx_status_t status = stream_.writev_at(0, request->offset, &vector, 1, &actual);
  ZX_DEBUG_ASSERT(actual <= request->data.count());
  if (status == ZX_OK) {
    vnode()->DidModifyStream();
  }
  completer.Reply(status, actual);
}

void StreamFileConnection::Seek(SeekRequestView request, SeekCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, 0u);
    return;
  }

  zx_off_t seek = 0u;
  zx_status_t status =
      stream_.seek(static_cast<zx_stream_seek_origin_t>(request->start), request->offset, &seek);
  completer.Reply(status, seek);
}

}  // namespace internal

}  // namespace fs

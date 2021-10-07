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

zx_status_t StreamFileConnection::ReadInternal(void* data, size_t len, size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.read) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (len > fio::wire::kMaxBuf) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_iovec_t vector = {
      .buffer = data,
      .capacity = len,
  };
  zx_status_t status = stream_.readv(0, &vector, 1, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
  }
  return status;
}

void StreamFileConnection::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadInternal(data, request->count, &actual);
  if (status != ZX_OK) {
    completer.Reply(status, fidl::VectorView<uint8_t>());
  } else {
    completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

void StreamFileConnection::Read2(Read2RequestView request, Read2Completer::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadInternal(data, request->count, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

zx_status_t StreamFileConnection::ReadAtInternal(void* data, size_t len, size_t offset,
                                                 size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.read) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (len > fio::wire::kMaxBuf) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_iovec_t vector = {
      .buffer = data,
      .capacity = len,
  };
  zx_status_t status = stream_.readv_at(0, offset, &vector, 1, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
  }
  return status;
}

void StreamFileConnection::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadAtInternal(data, request->count, request->offset, &actual);
  if (status != ZX_OK) {
    completer.Reply(status, fidl::VectorView<uint8_t>());
  } else {
    completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

void StreamFileConnection::ReadAt2(ReadAt2RequestView request, ReadAt2Completer::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadAtInternal(data, request->count, request->offset, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

zx_status_t StreamFileConnection::WriteInternal(const void* data, size_t len, size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileWrite] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.write) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_iovec_t vector = {
      .buffer = const_cast<void*>(data),
      .capacity = len,
  };
  uint32_t writev_options = options().flags.append ? ZX_STREAM_APPEND : 0;
  zx_status_t status = stream_.writev(writev_options, &vector, 1, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
    vnode()->DidModifyStream();
  }
  return status;
}

void StreamFileConnection::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  size_t actual = 0u;
  zx_status_t status = WriteInternal(request->data.data(), request->data.count(), &actual);
  completer.Reply(status, actual);
}

void StreamFileConnection::Write2(Write2RequestView request, Write2Completer::Sync& completer) {
  size_t actual = 0u;
  zx_status_t status = WriteInternal(request->data.data(), request->data.count(), &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(actual);
  }
}

zx_status_t StreamFileConnection::WriteAtInternal(const void* data, size_t len, size_t offset,
                                                  size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileWriteAt] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.write) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_iovec_t vector = {
      .buffer = const_cast<void*>(data),
      .capacity = len,
  };
  zx_status_t status = stream_.writev_at(0, offset, &vector, 1, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
    vnode()->DidModifyStream();
  }
  return status;
}

void StreamFileConnection::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
  size_t actual = 0;
  zx_status_t status =
      WriteAtInternal(request->data.data(), request->data.count(), request->offset, &actual);
  completer.Reply(status, actual);
}

void StreamFileConnection::WriteAt2(WriteAt2RequestView request,
                                    WriteAt2Completer::Sync& completer) {
  size_t actual = 0;
  zx_status_t status =
      WriteAtInternal(request->data.data(), request->data.count(), request->offset, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(actual);
  }
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

void StreamFileConnection::Seek2(Seek2RequestView request, Seek2Completer::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options());

  if (options().flags.node_reference) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }

  zx_off_t seek = 0u;
  zx_status_t status =
      stream_.seek(static_cast<zx_stream_seek_origin_t>(request->origin), request->offset, &seek);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(seek);
  }
}

}  // namespace internal

}  // namespace fs

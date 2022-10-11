// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/remote_file_connection.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
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

RemoteFileConnection::RemoteFileConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                           VnodeProtocol protocol, VnodeConnectionOptions options)
    : FileConnection(vfs, std::move(vnode), protocol, options) {}

zx_status_t RemoteFileConnection::ReadInternal(void* data, size_t len, size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.read) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (len > fio::wire::kMaxTransferSize) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t status = vnode()->Read(data, len, offset_, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
    offset_ += *out_actual;
  }
  return status;
}

void RemoteFileConnection::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadInternal(data, request->count, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

zx_status_t RemoteFileConnection::ReadAtInternal(void* data, size_t len, size_t offset,
                                                 size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.read) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (len > fio::wire::kMaxTransferSize) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  zx_status_t status = vnode()->Read(data, len, offset, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
  }
  return status;
}

void RemoteFileConnection::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
  uint8_t data[fio::wire::kMaxBuf];
  size_t actual = 0;
  zx_status_t status = ReadAtInternal(data, request->count, request->offset, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(data, actual));
  }
}

zx_status_t RemoteFileConnection::WriteInternal(const void* data, size_t len, size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileWrite] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.write) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_status_t status;
  if (options().flags.append) {
    size_t end = 0u;
    status = vnode()->Append(data, len, &end, out_actual);
    if (status == ZX_OK) {
      offset_ = end;
    }
  } else {
    status = vnode()->Write(data, len, offset_, out_actual);
    if (status == ZX_OK) {
      offset_ += *out_actual;
    }
  }
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
  }
  return status;
}

void RemoteFileConnection::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  size_t actual;
  zx_status_t status = WriteInternal(request->data.data(), request->data.count(), &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(actual);
  }
}

zx_status_t RemoteFileConnection::WriteAtInternal(const void* data, size_t len, size_t offset,
                                                  size_t* out_actual) {
  FS_PRETTY_TRACE_DEBUG("[FileWriteAt] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  if (!options().rights.write) {
    return ZX_ERR_BAD_HANDLE;
  }
  zx_status_t status = vnode()->Write(data, len, offset, out_actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(*out_actual <= len);
  }
  return status;
}

void RemoteFileConnection::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
  size_t actual = 0;
  zx_status_t status =
      WriteAtInternal(request->data.data(), request->data.count(), request->offset, &actual);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(actual);
  }
}

zx_status_t RemoteFileConnection::SeekInternal(fuchsia_io::wire::SeekOrigin origin,
                                               int64_t requested_offset) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options());

  if (options().flags.node_reference) {
    return ZX_ERR_BAD_HANDLE;
  }
  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode()->GetAttributes(&attr)) < 0) {
    return ZX_ERR_STOP;
  }
  size_t n;
  switch (origin) {
    case fio::wire::SeekOrigin::kStart:
      if (requested_offset < 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      n = requested_offset;
      break;
    case fio::wire::SeekOrigin::kCurrent:
      n = offset_ + requested_offset;
      if (requested_offset < 0) {
        // if negative seek
        if (n > offset_) {
          // wrapped around. attempt to seek before start
          return ZX_ERR_INVALID_ARGS;
        }
      } else {
        // positive seek
        if (n < offset_) {
          // wrapped around. overflow
          return ZX_ERR_INVALID_ARGS;
        }
      }
      break;
    case fio::wire::SeekOrigin::kEnd:
      n = attr.content_size + requested_offset;
      if (requested_offset < 0) {
        // if negative seek
        if (n > attr.content_size) {
          // wrapped around. attempt to seek before start
          return ZX_ERR_INVALID_ARGS;
        }
      } else {
        // positive seek
        if (n < attr.content_size) {
          // wrapped around
          return ZX_ERR_INVALID_ARGS;
        }
      }
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  offset_ = n;
  return ZX_OK;
}

void RemoteFileConnection::Seek(SeekRequestView request, SeekCompleter::Sync& completer) {
  zx_status_t status = SeekInternal(request->origin, request->offset);
  if (status == ZX_ERR_STOP) {
    completer.Close(ZX_ERR_INTERNAL);
  } else if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(offset_);
  }
}

void RemoteFileConnection::GetFlags(GetFlagsCompleter::Sync& completer) {
  zx::status result = NodeGetFlags();
  if (result.is_error()) {
    completer.Reply(result.status_value(), {});
  } else {
    completer.Reply(ZX_OK, result.value());
  }
}

void RemoteFileConnection::SetFlags(SetFlagsRequestView request,
                                    SetFlagsCompleter::Sync& completer) {
  completer.Reply(NodeSetFlags(request->flags).status_value());
}

}  // namespace internal

}  // namespace fs

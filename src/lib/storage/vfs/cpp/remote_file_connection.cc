// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/remote_file_connection.h"

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

RemoteFileConnection::RemoteFileConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                           VnodeProtocol protocol, VnodeConnectionOptions options)
    : FileConnection(vfs, std::move(vnode), protocol, options) {}

void RemoteFileConnection::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
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
  zx_status_t status = vnode()->Read(data, request->count, offset_, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= request->count);
    offset_ += actual;
  }
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void RemoteFileConnection::ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) {
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
  zx_status_t status = vnode()->Read(data, request->count, request->offset, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= request->count);
  }
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void RemoteFileConnection::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
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
  zx_status_t status;
  if (options().flags.append) {
    size_t end = 0u;
    status = vnode()->Append(request->data.data(), request->data.count(), &end, &actual);
    if (status == ZX_OK) {
      offset_ = end;
    }
  } else {
    status = vnode()->Write(request->data.data(), request->data.count(), offset_, &actual);
    if (status == ZX_OK) {
      offset_ += actual;
    }
  }
  ZX_DEBUG_ASSERT(actual <= request->data.count());
  completer.Reply(status, actual);
}

void RemoteFileConnection::WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) {
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
  zx_status_t status =
      vnode()->Write(request->data.data(), request->data.count(), request->offset, &actual);
  ZX_DEBUG_ASSERT(actual <= request->data.count());
  completer.Reply(status, actual);
}

void RemoteFileConnection::Seek(SeekRequestView request, SeekCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, offset_);
    return;
  }
  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode()->GetAttributes(&attr)) < 0) {
    return completer.Close(r);
  }
  size_t n;
  switch (request->start) {
    case fio::wire::SeekOrigin::kStart:
      if (request->offset < 0) {
        completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
        return;
      }
      n = request->offset;
      break;
    case fio::wire::SeekOrigin::kCurrent:
      n = offset_ + request->offset;
      if (request->offset < 0) {
        // if negative seek
        if (n > offset_) {
          // wrapped around. attempt to seek before start
          completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
          return;
        }
      } else {
        // positive seek
        if (n < offset_) {
          // wrapped around. overflow
          completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
          return;
        }
      }
      break;
    case fio::wire::SeekOrigin::kEnd:
      n = attr.content_size + request->offset;
      if (request->offset < 0) {
        // if negative seek
        if (n > attr.content_size) {
          // wrapped around. attempt to seek before start
          completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
          return;
        }
      } else {
        // positive seek
        if (n < attr.content_size) {
          // wrapped around
          completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
          return;
        }
      }
      break;
    default:
      completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
      return;
  }
  offset_ = n;
  completer.Reply(ZX_OK, offset_);
}

}  // namespace internal

}  // namespace fs

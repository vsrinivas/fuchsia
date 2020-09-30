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
#include <fs/internal/remote_file_connection.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

namespace internal {

RemoteFileConnection::RemoteFileConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                           VnodeProtocol protocol, VnodeConnectionOptions options)
    : FileConnection(vfs, std::move(vnode), protocol, options) {}

void RemoteFileConnection::Read(uint64_t count, ReadCompleter::Sync completer) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (!options().rights.read) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (count > fio::MAX_BUF) {
    completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
    return;
  }
  uint8_t data[fio::MAX_BUF];
  size_t actual = 0;
  zx_status_t status = vnode()->Read(data, count, offset_, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= count);
    offset_ += actual;
  }
  completer.Reply(status, fidl::VectorView(fidl::unowned_ptr(data), actual));
}

void RemoteFileConnection::ReadAt(uint64_t count, uint64_t offset,
                                  ReadAtCompleter::Sync completer) {
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (!options().rights.read) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (count > fio::MAX_BUF) {
    completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
    return;
  }
  uint8_t data[fio::MAX_BUF];
  size_t actual = 0;
  zx_status_t status = vnode()->Read(data, count, offset, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= count);
  }
  completer.Reply(status, fidl::VectorView(fidl::unowned_ptr(data), actual));
}

void RemoteFileConnection::Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) {
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
    status = vnode()->Append(data.data(), data.count(), &end, &actual);
    if (status == ZX_OK) {
      offset_ = end;
    }
  } else {
    status = vnode()->Write(data.data(), data.count(), offset_, &actual);
    if (status == ZX_OK) {
      offset_ += actual;
    }
  }
  ZX_DEBUG_ASSERT(actual <= data.count());
  completer.Reply(status, actual);
}

void RemoteFileConnection::WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
                                   WriteAtCompleter::Sync completer) {
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
  zx_status_t status = vnode()->Write(data.data(), data.count(), offset, &actual);
  ZX_DEBUG_ASSERT(actual <= data.count());
  completer.Reply(status, actual);
}

void RemoteFileConnection::Seek(int64_t offset, ::llcpp::fuchsia::io::SeekOrigin start,
                                SeekCompleter::Sync completer) {
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
  switch (start) {
    case fio::SeekOrigin::START:
      if (offset < 0) {
        completer.Reply(ZX_ERR_INVALID_ARGS, offset_);
        return;
      }
      n = offset;
      break;
    case fio::SeekOrigin::CURRENT:
      n = offset_ + offset;
      if (offset < 0) {
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
    case fio::SeekOrigin::END:
      n = attr.content_size + offset;
      if (offset < 0) {
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

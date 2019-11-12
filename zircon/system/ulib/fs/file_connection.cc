// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/internal/file_connection.h>

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
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
#include <fs/handler.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

zx_status_t FileConnection::HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  zx_status_t status = fuchsia_io_File_try_dispatch(this, txn, msg, &kOps);
  if (status != ZX_ERR_NOT_SUPPORTED) {
    return status;
  }
  return vnode()->HandleFsSpecificMessage(msg, txn);
}

zx_status_t FileConnection::Clone(uint32_t clone_flags, zx_handle_t object) {
  return Connection::NodeClone(clone_flags, object);
}

zx_status_t FileConnection::Close(fidl_txn_t* txn) { return Connection::NodeClose(txn); }

zx_status_t FileConnection::Describe(fidl_txn_t* txn) { return Connection::NodeDescribe(txn); }

zx_status_t FileConnection::Sync(fidl_txn_t* txn) { return Connection::NodeSync(txn); }

zx_status_t FileConnection::GetAttr(fidl_txn_t* txn) { return Connection::NodeGetAttr(txn); }

zx_status_t FileConnection::SetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                                    fidl_txn_t* txn) {
  return Connection::NodeSetAttr(flags, attributes, txn);
}

zx_status_t FileConnection::NodeGetFlags(fidl_txn_t* txn) {
  return Connection::NodeNodeGetFlags(txn);
}

zx_status_t FileConnection::NodeSetFlags(uint32_t flags, fidl_txn_t* txn) {
  return Connection::NodeNodeSetFlags(flags, txn);
}

zx_status_t FileConnection::Read(uint64_t count, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_FileRead_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (!options().rights.read) {
    return fuchsia_io_FileRead_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (count > ZXFIDL_MAX_MSG_BYTES) {
    return fuchsia_io_FileRead_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
  }
  uint8_t data[count];
  size_t actual = 0;
  zx_status_t status = vnode()->Read(data, count, offset_, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= count);
    offset_ += actual;
  }
  return fuchsia_io_FileRead_reply(txn, status, data, actual);
}

zx_status_t FileConnection::ReadAt(uint64_t count, uint64_t offset, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (!options().rights.read) {
    return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (count > ZXFIDL_MAX_MSG_BYTES) {
    return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
  }
  uint8_t data[count];
  size_t actual = 0;
  zx_status_t status = vnode()->Read(data, count, offset, &actual);
  if (status == ZX_OK) {
    ZX_DEBUG_ASSERT(actual <= count);
  }
  return fuchsia_io_FileReadAt_reply(txn, status, data, actual);
}

zx_status_t FileConnection::Write(const uint8_t* data_data, size_t data_count, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileWrite] options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  if (!options().rights.write) {
    return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }

  size_t actual = 0;
  zx_status_t status;
  if (options().flags.append) {
    size_t end;
    status = vnode()->Append(data_data, data_count, &end, &actual);
    if (status == ZX_OK) {
      offset_ = end;
    }
  } else {
    status = vnode()->Write(data_data, data_count, offset_, &actual);
    if (status == ZX_OK) {
      offset_ += actual;
    }
  }
  ZX_DEBUG_ASSERT(actual <= data_count);
  return fuchsia_io_FileWrite_reply(txn, status, actual);
}

zx_status_t FileConnection::WriteAt(const uint8_t* data_data, size_t data_count, uint64_t offset,
                                    fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileWriteAt] options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  if (!options().rights.write) {
    return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  size_t actual = 0;
  zx_status_t status = vnode()->Write(data_data, data_count, offset, &actual);
  ZX_DEBUG_ASSERT(actual <= data_count);
  return fuchsia_io_FileWriteAt_reply(txn, status, actual);
}

zx_status_t FileConnection::Seek(int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options());

  static_assert(SEEK_SET == fuchsia_io_SeekOrigin_START, "");
  static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_CURRENT, "");
  static_assert(SEEK_END == fuchsia_io_SeekOrigin_END, "");

  if (options().flags.node_reference) {
    return fuchsia_io_FileSeek_reply(txn, ZX_ERR_BAD_HANDLE, offset_);
  }
  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode()->GetAttributes(&attr)) < 0) {
    return r;
  }
  size_t n;
  switch (start) {
    case SEEK_SET:
      if (offset < 0) {
        return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
      }
      n = offset;
      break;
    case SEEK_CUR:
      n = offset_ + offset;
      if (offset < 0) {
        // if negative seek
        if (n > offset_) {
          // wrapped around. attempt to seek before start
          return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
      } else {
        // positive seek
        if (n < offset_) {
          // wrapped around. overflow
          return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
      }
      break;
    case SEEK_END:
      n = attr.content_size + offset;
      if (offset < 0) {
        // if negative seek
        if (n > attr.content_size) {
          // wrapped around. attempt to seek before start
          return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
      } else {
        // positive seek
        if (n < attr.content_size) {
          // wrapped around
          return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
      }
      break;
    default:
      return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
  }
  offset_ = n;
  return fuchsia_io_FileSeek_reply(txn, ZX_OK, offset_);
}

zx_status_t FileConnection::Truncate(uint64_t length, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileTruncate] options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
  }

  zx_status_t status = vnode()->Truncate(length);
  return fuchsia_io_FileTruncate_reply(txn, status);
}

zx_status_t FileConnection::GetFlags(fidl_txn_t* txn) {
  uint32_t flags = options().ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS);
  return fuchsia_io_FileGetFlags_reply(txn, ZX_OK, flags);
}

zx_status_t FileConnection::SetFlags(uint32_t flags, fidl_txn_t* txn) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  set_append(options.flags.append);
  return fuchsia_io_FileSetFlags_reply(txn, ZX_OK);
}

zx_status_t FileConnection::GetBuffer(uint32_t flags, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileGetBuffer] our options: ", options(),
                        ", incoming flags: ", ZxFlags(flags));

  if (options().flags.node_reference) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_BAD_HANDLE, nullptr);
  }

  if ((flags & fuchsia_io_VMO_FLAG_PRIVATE) && (flags & fuchsia_io_VMO_FLAG_EXACT)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_INVALID_ARGS, nullptr);
  } else if ((options().flags.append) && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.write && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.execute && (flags & fuchsia_io_VMO_FLAG_EXEC)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options().rights.read) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  }

  fuchsia_mem_Buffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  zx_status_t status = vnode()->GetVmo(flags, &buffer.vmo, &buffer.size);
  return fuchsia_io_FileGetBuffer_reply(txn, status, status == ZX_OK ? &buffer : nullptr);
}

}  // namespace internal

}  // namespace fs

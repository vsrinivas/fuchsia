// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <zircon/syscalls.h>

namespace fio = llcpp::fuchsia::io;

namespace {

// C++ wrapper around zxio_remote_t.
class Remote {
 public:
  explicit Remote(zxio_t* io) : rio_(reinterpret_cast<zxio_remote_t*>(io)) {}

  zx::unowned_channel control() const { return zx::unowned_channel(rio_->control); }

  zx::handle Release() {
    zx::handle control(rio_->control);
    rio_->control = ZX_HANDLE_INVALID;
    if (rio_->event != ZX_HANDLE_INVALID) {
      zx_handle_close(rio_->event);
      rio_->event = ZX_HANDLE_INVALID;
    }
    return control;
  }

 private:
  zxio_remote_t* rio_;
};

zx_status_t zxio_remote_close(zxio_t* io) {
  Remote rio(io);
  auto result = fio::Node::Call::Close(rio.control());
  rio.Release().reset();
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_release(zxio_t* io, zx_handle_t* out_handle) {
  Remote rio(io);
  *out_handle = rio.Release().release();
  return ZX_OK;
}

zx_status_t zxio_remote_clone(zxio_t* io, zx_handle_t* out_handle) {
  Remote rio(io);
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t flags = fio::CLONE_FLAG_SAME_RIGHTS;
  auto result = fio::Node::Call::Clone(rio.control(), flags, std::move(remote));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  *out_handle = local.release();
  return ZX_OK;
}

zx_status_t zxio_remote_sync(zxio_t* io) {
  Remote rio(io);
  auto result = fio::Node::Call::Sync(rio.control());
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  Remote rio(io);
  auto result = fio::Node::Call::GetAttr(rio.control());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (auto status = result.Unwrap()->s; status != ZX_OK) {
    return status;
  }
  *out_attr = result.Unwrap()->attributes;
  return ZX_OK;
}

zx_status_t zxio_remote_attr_set(zxio_t* io, uint32_t flags, const zxio_node_attr_t* attr) {
  Remote rio(io);
  auto result = fio::Node::Call::SetAttr(rio.control(), flags, *attr);
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_read_once(const Remote& rio, uint8_t* buffer, size_t capacity,
                                  size_t* out_actual) {
  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fio::File::ReadRequest> request_buffer;
  fidl::Buffer<fio::File::ReadResponse> response_buffer;
  auto result =
      fio::File::Call::Read(rio.control(), request_buffer.view(), capacity, response_buffer.view());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  fio::File::ReadResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  const auto& data = response->data;
  if (data.count() > capacity) {
    return ZX_ERR_IO;
  }
  memcpy(buffer, data.begin(), data.count());
  *out_actual = data.count();
  return ZX_OK;
}

zx_status_t zxio_remote_read(zxio_t* io, void* data, size_t capacity, size_t* out_actual) {
  Remote rio(io);
  uint8_t* buffer = static_cast<uint8_t*>(data);
  size_t received = 0;
  while (capacity > 0) {
    size_t chunk = (capacity > fio::MAX_BUF) ? fio::MAX_BUF : capacity;
    size_t actual = 0;
    zx_status_t status = zxio_remote_read_once(rio, buffer, chunk, &actual);
    if (status != ZX_OK) {
      return status;
    }
    received += actual;
    buffer += actual;
    capacity -= actual;
    if (chunk != actual) {
      break;
    }
  }
  *out_actual = received;
  return ZX_OK;
}

zx_status_t zxio_remote_read_once_at(const Remote& rio, size_t offset, uint8_t* buffer,
                                     size_t capacity, size_t* out_actual) {
  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fio::File::ReadAtRequest> request_buffer;
  fidl::Buffer<fio::File::ReadAtResponse> response_buffer;
  auto result = fio::File::Call::ReadAt(rio.control(), request_buffer.view(), capacity, offset,
                                        response_buffer.view());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  fio::File::ReadAtResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  const auto& data = response->data;
  if (data.count() > capacity) {
    return ZX_ERR_IO;
  }
  memcpy(buffer, data.begin(), data.count());
  *out_actual = data.count();
  return ZX_OK;
}

zx_status_t zxio_remote_read_at(zxio_t* io, size_t offset, void* data, size_t capacity,
                                size_t* out_actual) {
  Remote rio(io);
  uint8_t* buffer = static_cast<uint8_t*>(data);
  size_t received = 0;
  while (capacity > 0) {
    size_t chunk = (capacity > fio::MAX_BUF) ? fio::MAX_BUF : capacity;
    size_t actual = 0;
    zx_status_t status = zxio_remote_read_once_at(rio, offset, buffer, chunk, &actual);
    if (status != ZX_OK) {
      return status;
    }
    offset += actual;
    received += actual;
    buffer += actual;
    capacity -= actual;
    if (chunk != actual) {
      break;
    }
  }
  *out_actual = received;
  return ZX_OK;
}

zx_status_t zxio_remote_write_once(const Remote& rio, const uint8_t* buffer, size_t capacity,
                                   size_t* out_actual) {
  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fio::File::WriteRequest> request_buffer;
  fidl::Buffer<fio::File::WriteResponse> response_buffer;
  auto result = fio::File::Call::Write(rio.control(), request_buffer.view(),
                                       fidl::VectorView(capacity, const_cast<uint8_t*>(buffer)),
                                       response_buffer.view());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  auto response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  if (response->actual > capacity) {
    return ZX_ERR_IO;
  }
  *out_actual = response->actual;
  return ZX_OK;
}

zx_status_t zxio_remote_write(zxio_t* io, const void* data, size_t capacity, size_t* out_actual) {
  Remote rio(io);
  const uint8_t* buffer = static_cast<const uint8_t*>(data);
  size_t sent = 0u;
  while (capacity > 0) {
    size_t chunk = (capacity > fio::MAX_BUF) ? fio::MAX_BUF : capacity;
    size_t actual = 0u;
    zx_status_t status = zxio_remote_write_once(rio, buffer, chunk, &actual);
    if (status != ZX_OK) {
      return status;
    }
    sent += actual;
    buffer += actual;
    capacity -= actual;
    if (chunk != actual) {
      break;
    }
  }
  *out_actual = sent;
  return ZX_OK;
}

zx_status_t zxio_remote_write_once_at(const Remote& rio, size_t offset, const uint8_t* buffer,
                                      size_t capacity, size_t* out_actual) {
  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fio::File::WriteAtRequest> request_buffer;
  fidl::Buffer<fio::File::WriteAtResponse> response_buffer;
  auto result = fio::File::Call::WriteAt(rio.control(), request_buffer.view(),
                                         fidl::VectorView(capacity, const_cast<uint8_t*>(buffer)),
                                         offset, response_buffer.view());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  auto response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  if (response->actual > capacity) {
    return ZX_ERR_IO;
  }
  *out_actual = response->actual;
  return ZX_OK;
}

zx_status_t zxio_remote_write_at(zxio_t* io, size_t offset, const void* data, size_t capacity,
                                 size_t* out_actual) {
  Remote rio(io);
  const uint8_t* buffer = static_cast<const uint8_t*>(data);
  size_t sent = 0u;
  while (capacity > 0) {
    size_t chunk = (capacity > fio::MAX_BUF) ? fio::MAX_BUF : capacity;
    size_t actual = 0u;
    zx_status_t status = zxio_remote_write_once_at(rio, offset, buffer, chunk, &actual);
    if (status != ZX_OK) {
      return status;
    }
    sent += actual;
    buffer += actual;
    offset += actual;
    capacity -= actual;
    if (chunk != actual) {
      break;
    }
  }
  *out_actual = sent;
  return ZX_OK;
}

zx_status_t zxio_remote_seek(zxio_t* io, size_t offset, zxio_seek_origin_t start,
                             size_t* out_offset) {
  Remote rio(io);
  auto result = fio::File::Call::Seek(rio.control(), offset, static_cast<fio::SeekOrigin>(start));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (auto status = result.Unwrap()->s; status != ZX_OK) {
    return status;
  }
  *out_offset = result.Unwrap()->offset;
  return ZX_OK;
}

zx_status_t zxio_remote_truncate(zxio_t* io, size_t length) {
  Remote rio(io);
  auto result = fio::File::Call::Truncate(rio.control(), length);
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_flags_get(zxio_t* io, uint32_t* out_flags) {
  Remote rio(io);
  auto result = fio::File::Call::GetFlags(rio.control());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (auto status = result.Unwrap()->s; status != ZX_OK) {
    return status;
  }
  *out_flags = result.Unwrap()->flags;
  return ZX_OK;
}

zx_status_t zxio_remote_flags_set(zxio_t* io, uint32_t flags) {
  Remote rio(io);
  auto result = fio::File::Call::SetFlags(rio.control(), flags);
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                                size_t* out_size) {
  Remote rio(io);
  auto result = fio::File::Call::GetBuffer(rio.control(), flags);
  if (result.status() != ZX_OK) {
    return result.status();
  }
  fio::File::GetBufferResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  llcpp::fuchsia::mem::Buffer* buffer = response->buffer;
  if (!buffer) {
    return ZX_ERR_IO;
  }
  if (buffer->vmo == ZX_HANDLE_INVALID) {
    return ZX_ERR_IO;
  }
  *out_vmo = buffer->vmo.release();
  *out_size = buffer->size;
  return ZX_OK;
}

zx_status_t zxio_remote_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                   zx_handle_t request) {
  Remote rio(io);
  auto result = fio::Directory::Call::Open(
      rio.control(), flags, mode, fidl::StringView(strlen(path), path), zx::channel(request));
  return result.status();
}

zx_status_t zxio_remote_unlink(zxio_t* io, const char* path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Unlink(rio.control(), fidl::StringView(strlen(path), path));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_token_get(zxio_t* io, zx_handle_t* out_token) {
  Remote rio(io);
  auto result = fio::Directory::Call::GetToken(rio.control());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (auto status = result.Unwrap()->s; status != ZX_OK) {
    return status;
  }
  *out_token = result.Unwrap()->token.release();
  return ZX_OK;
}

zx_status_t zxio_remote_rename(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                               const char* dst_path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Rename(
      rio.control(), fidl::StringView(strlen(src_path), src_path), zx::handle(dst_token),
      fidl::StringView(strlen(dst_path), dst_path));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_link(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                             const char* dst_path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Link(
      rio.control(), fidl::StringView(strlen(src_path), src_path), zx::handle(dst_token),
      fidl::StringView(strlen(dst_path), dst_path));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_readdir(zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
  Remote rio(io);
  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fio::Directory::ReadDirentsRequest> request_buffer;
  fidl::Buffer<fio::Directory::ReadDirentsResponse> response_buffer;
  auto result = fio::Directory::Call::ReadDirents(rio.control(), request_buffer.view(), capacity,
                                                  response_buffer.view());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  fio::Directory::ReadDirentsResponse* response = result.Unwrap();
  if (response->s != ZX_OK) {
    return response->s;
  }
  const auto& dirents = response->dirents;
  if (dirents.count() > capacity) {
    return ZX_ERR_IO;
  }
  memcpy(buffer, dirents.data(), dirents.count());
  *out_actual = dirents.count();
  return response->s;
}

zx_status_t zxio_remote_rewind(zxio_t* io) {
  Remote rio(io);
  auto result = fio::Directory::Call::Rewind(rio.control());
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_isatty(zxio_t* io, bool* tty) {
  Remote rio(io);
  auto result = fio::Node::Call::Describe(rio.control());
  if (result.status() != ZX_OK) {
    return result.status();
  }
  *tty = result.Unwrap()->info.is_tty();
  return ZX_OK;
}

}  // namespace

static constexpr zxio_ops_t zxio_remote_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.clone = zxio_remote_clone;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_attr_get;
  ops.attr_set = zxio_remote_attr_set;
  ops.read = zxio_remote_read;
  ops.read_at = zxio_remote_read_at;
  ops.write = zxio_remote_write;
  ops.write_at = zxio_remote_write_at;
  ops.seek = zxio_remote_seek;
  ops.truncate = zxio_remote_truncate;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.vmo_get = zxio_remote_vmo_get;
  ops.open_async = zxio_remote_open_async;
  ops.unlink = zxio_remote_unlink;
  ops.token_get = zxio_remote_token_get;
  ops.rename = zxio_remote_rename;
  ops.link = zxio_remote_link;
  ops.readdir = zxio_remote_readdir;
  ops.rewind = zxio_remote_rewind;
  ops.isatty = zxio_remote_isatty;
  return ops;
}();

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t event) {
  zxio_remote_t* remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_remote_ops);
  remote->control = control;
  remote->event = event;
  return ZX_OK;
}

namespace {

zx_status_t zxio_dir_read(zxio_t* io, void* data, size_t capacity, size_t* out_actual) {
  if (capacity == 0) {
    // zero-sized reads to directories should always succeed
    *out_actual = 0;
    return ZX_OK;
  }
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_dir_read_at(zxio_t* io, size_t offset, void* data, size_t capacity,
                             size_t* out_actual) {
  if (capacity == 0) {
    // zero-sized reads to directories should always succeed
    *out_actual = 0;
    return ZX_OK;
  }
  return ZX_ERR_WRONG_TYPE;
}

}  // namespace

static constexpr zxio_ops_t zxio_dir_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.clone = zxio_remote_clone;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_attr_get;
  ops.attr_set = zxio_remote_attr_set;
  // use specialized read functions that succeed for zero-sized reads.
  ops.read = zxio_dir_read;
  ops.read_at = zxio_dir_read_at;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.open_async = zxio_remote_open_async;
  ops.unlink = zxio_remote_unlink;
  ops.token_get = zxio_remote_token_get;
  ops.rename = zxio_remote_rename;
  ops.link = zxio_remote_link;
  ops.readdir = zxio_remote_readdir;
  ops.rewind = zxio_remote_rewind;
  return ops;
}();

zx_status_t zxio_dir_init(zxio_storage_t* storage, zx_handle_t control) {
  zxio_remote_t* remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_dir_ops);
  remote->control = control;
  remote->event = ZX_HANDLE_INVALID;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_file_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.clone = zxio_remote_clone;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_attr_get;
  ops.attr_set = zxio_remote_attr_set;
  ops.read = zxio_remote_read;
  ops.read_at = zxio_remote_read_at;
  ops.write = zxio_remote_write;
  ops.write_at = zxio_remote_write_at;
  ops.seek = zxio_remote_seek;
  ops.truncate = zxio_remote_truncate;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  return ops;
}();

zx_status_t zxio_file_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t event) {
  zxio_remote_t* remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_file_ops);
  remote->control = control;
  remote->event = event;
  return ZX_OK;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include "private.h"

namespace fio = llcpp::fuchsia::io;

namespace {

// C++ wrapper around zxio_remote_t.
class Remote {
 public:
  explicit Remote(zxio_t* io) : rio_(reinterpret_cast<zxio_remote_t*>(io)) {}

  [[nodiscard]] zx::unowned_channel control() const { return zx::unowned_channel(rio_->control); }

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

template <typename F>
static zx_status_t zxio_remote_do_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                         zxio_flags_t flags, size_t* out_actual, F fn) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  Remote rio(io);

  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* data, size_t capacity, size_t* out_actual) {
                          auto buffer = static_cast<uint8_t*>(data);
                          size_t total = 0;
                          while (capacity > 0) {
                            size_t chunk = std::min(capacity, fio::MAX_BUF);
                            size_t actual;
                            zx_status_t status = fn(rio.control(), buffer, chunk, &actual);
                            if (status != ZX_OK) {
                              return status;
                            }
                            total += actual;
                            if (actual != chunk) {
                              break;
                            }
                            buffer += actual;
                            capacity -= actual;
                          }
                          *out_actual = total;
                          return ZX_OK;
                        });
}

zx_status_t zxio_remote_read_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  return zxio_remote_do_vector(
      io, vector, vector_count, flags, out_actual,
      [](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::Buffer<fio::File::ReadRequest> request_buffer;
        fidl::Buffer<fio::File::ReadResponse> response_buffer;
        auto result = fio::File::Call::Read(std::move(control), request_buffer.view(), capacity,
                                            response_buffer.view());
        zx_status_t status;
        if ((status = result.status()) != ZX_OK) {
          return status;
        }
        if ((status = result->s) != ZX_OK) {
          return status;
        }
        const auto& data = result->data;
        size_t actual = data.count();
        if (actual > capacity) {
          return ZX_ERR_IO;
        }
        memcpy(buffer, data.begin(), actual);
        *out_actual = actual;
        return ZX_OK;
      });
}

zx_status_t zxio_remote_read_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                       size_t vector_count, zxio_flags_t flags,
                                       size_t* out_actual) {
  return zxio_remote_do_vector(
      io, vector, vector_count, flags, out_actual,
      [&offset](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        fidl::Buffer<fio::File::ReadAtRequest> request_buffer;
        fidl::Buffer<fio::File::ReadAtResponse> response_buffer;
        auto result = fio::File::Call::ReadAt(std::move(control), request_buffer.view(), capacity,
                                              offset, response_buffer.view());
        zx_status_t status;
        if ((status = result.status()) != ZX_OK) {
          return status;
        }
        if ((status = result->s) != ZX_OK) {
          return status;
        }
        const auto& data = result->data;
        size_t actual = data.count();
        if (actual > capacity) {
          return ZX_ERR_IO;
        }
        offset += actual;
        memcpy(buffer, data.begin(), actual);
        *out_actual = actual;
        return ZX_OK;
      });
}

zx_status_t zxio_remote_write_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                     zxio_flags_t flags, size_t* out_actual) {
  return zxio_remote_do_vector(
      io, vector, vector_count, flags, out_actual,
      [](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::Buffer<fio::File::WriteRequest> request_buffer;
        fidl::Buffer<fio::File::WriteResponse> response_buffer;
        auto result =
            fio::File::Call::Write(std::move(control), request_buffer.view(),
                                   fidl::VectorView(buffer, capacity), response_buffer.view());
        zx_status_t status;
        if ((status = result.status()) != ZX_OK) {
          return status;
        }
        if ((status = result->s) != ZX_OK) {
          return status;
        }
        size_t actual = result->actual;
        if (actual > capacity) {
          return ZX_ERR_IO;
        }
        *out_actual = actual;
        return ZX_OK;
      });
}

zx_status_t zxio_remote_write_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                        size_t vector_count, zxio_flags_t flags,
                                        size_t* out_actual) {
  return zxio_remote_do_vector(
      io, vector, vector_count, flags, out_actual,
      [&offset](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::Buffer<fio::File::WriteAtRequest> request_buffer;
        fidl::Buffer<fio::File::WriteAtResponse> response_buffer;
        auto result = fio::File::Call::WriteAt(std::move(control), request_buffer.view(),
                                               fidl::VectorView(buffer, capacity), offset,
                                               response_buffer.view());
        zx_status_t status;
        if ((status = result.status()) != ZX_OK) {
          return status;
        }
        if ((status = result->s) != ZX_OK) {
          return status;
        }
        size_t actual = result->actual;
        if (actual > capacity) {
          return ZX_ERR_IO;
        }
        offset += actual;
        *out_actual = actual;
        return ZX_OK;
      });
}

zx_status_t zxio_remote_seek(zxio_t* io, zx_off_t offset, zxio_seek_origin_t start,
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
                                   size_t path_len, zx_handle_t request) {
  Remote rio(io);
  auto result = fio::Directory::Call::Open(
      rio.control(), flags, mode, fidl::StringView(path, path_len), zx::channel(request));
  return result.status();
}

zx_status_t zxio_remote_unlink(zxio_t* io, const char* path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Unlink(rio.control(), fidl::StringView(path, strlen(path)));
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
      rio.control(), fidl::StringView(src_path, strlen(src_path)), zx::handle(dst_token),
      fidl::StringView(dst_path, strlen(dst_path)));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_link(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                             const char* dst_path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Link(
      rio.control(), fidl::StringView(src_path, strlen(src_path)), zx::handle(dst_token),
      fidl::StringView(dst_path, strlen(dst_path)));
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
  ops.read_vector = zxio_remote_read_vector;
  ops.read_vector_at = zxio_remote_read_vector_at;
  ops.write_vector = zxio_remote_write_vector;
  ops.write_vector_at = zxio_remote_write_vector_at;
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
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_remote_ops);
  remote->control = control;
  remote->event = event;
  return ZX_OK;
}

namespace {

zx_status_t zxio_dir_read_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                 zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return zxio_do_vector(vector, vector_count, out_actual,
                        [](void* buffer, size_t capacity, size_t* out_actual) {
                          if (capacity > 0) {
                            return ZX_ERR_WRONG_TYPE;
                          }
                          *out_actual = 0;
                          return ZX_OK;
                        });
}

zx_status_t zxio_dir_read_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                    size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  return zxio_dir_read_vector(io, vector, vector_count, flags, out_actual);
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
  ops.read_vector = zxio_dir_read_vector;
  ops.read_vector_at = zxio_dir_read_vector_at;
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
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
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
  ops.read_vector = zxio_remote_read_vector;
  ops.read_vector_at = zxio_remote_read_vector_at;
  ops.write_vector = zxio_remote_write_vector;
  ops.write_vector_at = zxio_remote_write_vector_at;
  ops.seek = zxio_remote_seek;
  ops.truncate = zxio_remote_truncate;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.vmo_get = zxio_remote_vmo_get;
  return ops;
}();

zx_status_t zxio_file_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t event) {
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_file_ops);
  remote->control = control;
  remote->event = event;
  return ZX_OK;
}

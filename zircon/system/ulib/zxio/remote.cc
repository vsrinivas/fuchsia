// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include "lib/zxio/types.h"
#include "private.h"

namespace fio = llcpp::fuchsia::io;

namespace {

// Implementation of |zxio_dirent_iterator_t| for |fuchsia.io| v1.
class DirentIteratorImpl {
 public:
  explicit DirentIteratorImpl(zxio_t* io)
      : io_(reinterpret_cast<zxio_remote_t*>(io)), boxed_(std::make_unique<Boxed>()) {
    static_assert(offsetof(DirentIteratorImpl, io_) == 0,
                  "zxio_dirent_iterator_t requires first field of implementation to be zxio_t");
    (void)opaque_;
  }

  ~DirentIteratorImpl() { fio::Directory::Call::Rewind(zx::unowned_channel(io_->control)); }

  zx_status_t Next(zxio_dirent_t** out_entry) {
    if (index_ >= count_) {
      zx_status_t status = RemoteReadDirents();
      if (status != ZX_OK) {
        return status;
      }
      if (count_ == 0) {
        return ZX_ERR_NOT_FOUND;
      }
      index_ = 0;
    }

    // The format of the packed dirent structure, taken from io.fidl.
    struct dirent {
      // Describes the inode of the entry.
      uint64_t ino;
      // Describes the length of the dirent name in bytes.
      uint8_t size;
      // Describes the type of the entry. Aligned with the
      // POSIX d_type values. Use `DIRENT_TYPE_*` constants.
      uint8_t type;
      // Unterminated name of entry.
      char name[0];
    } __PACKED;

    auto entry = reinterpret_cast<const dirent*>(&data_[index_]);

    // Check if we can read the entry size.
    if (index_ + sizeof(dirent) > count_) {
      // Should not happen
      return ZX_ERR_INTERNAL;
    }

    size_t entry_size = sizeof(dirent) + entry->size;

    // Check if we can read the whole entry.
    if (index_ + entry_size > count_) {
      // Should not happen
      return ZX_ERR_INTERNAL;
    }

    // Check that the name length is within bounds.
    if (entry->size > fio::MAX_FILENAME) {
      return ZX_ERR_INVALID_ARGS;
    }

    index_ += entry_size;

    boxed_->current_entry = {};
    boxed_->current_entry.name = boxed_->current_entry_name;
    ZXIO_DIRENT_SET(boxed_->current_entry, protocols, DTypeToProtocols(entry->type));
    ZXIO_DIRENT_SET(boxed_->current_entry, id, entry->ino);
    boxed_->current_entry.name_length = entry->size;
    memcpy(boxed_->current_entry_name, entry->name, entry->size);
    boxed_->current_entry_name[entry->size] = '\0';
    *out_entry = &boxed_->current_entry;

    return ZX_OK;
  }

 private:
  zx_status_t RemoteReadDirents() {
    auto result = fio::Directory::Call::ReadDirents(zx::unowned_channel(io_->control),
                                                    boxed_->request_buffer.view(), kBufferSize,
                                                    boxed_->response_buffer.view());
    if (result.status() != ZX_OK) {
      return result.status();
    }
    fio::Directory::ReadDirentsResponse* response = result.Unwrap();
    if (response->s != ZX_OK) {
      return response->s;
    }
    const auto& dirents = response->dirents;
    if (dirents.count() > kBufferSize) {
      return ZX_ERR_IO;
    }
    data_ = dirents.data();
    count_ = dirents.count();
    return ZX_OK;
  }

  zxio_node_protocols_t DTypeToProtocols(uint8_t type) {
    switch (type) {
      case DT_BLK:
        return ZXIO_NODE_PROTOCOL_DEVICE;
      case DT_CHR:
        return ZXIO_NODE_PROTOCOL_TTY;
      case DT_DIR:
        return ZXIO_NODE_PROTOCOL_DIRECTORY;
      case DT_FIFO:
        return ZXIO_NODE_PROTOCOL_PIPE;
      case DT_LNK:
        // Not supported.
        return ZXIO_NODE_PROTOCOL_NONE;
      case DT_REG:
        return ZXIO_NODE_PROTOCOL_FILE;
      case DT_SOCK:
        return ZXIO_NODE_PROTOCOL_POSIX_SOCKET;
      default:
        return ZXIO_NODE_PROTOCOL_NONE;
    }
  }

  // The maximum buffer size that is supported by |fuchsia.io/Directory.ReadDirents|.
  static constexpr size_t kBufferSize = fio::MAX_BUF;

  // This large structure is heap-allocated once, to be reused by subsequent
  // ReadDirents calls.
  struct Boxed {
    Boxed() = default;

    // Buffers used by the FIDL calls.
    fidl::Buffer<fio::Directory::ReadDirentsRequest> request_buffer;
    fidl::Buffer<fio::Directory::ReadDirentsResponse> response_buffer;

    // At each |zxio_dirent_iterator_next| call, we would extract the next
    // dirent segment from |response_buffer|, and populate |current_entry|
    // and |current_entry_name|.
    zxio_dirent_t current_entry;
    char current_entry_name[fio::MAX_FILENAME + 1] = {};
  };

  zxio_remote_t* io_;
  std::unique_ptr<Boxed> boxed_;
  const uint8_t* data_ = nullptr;
  uint64_t count_ = 0;
  uint64_t index_ = 0;
  uint64_t opaque_[3];
};

static_assert(sizeof(zxio_dirent_iterator_t) == sizeof(DirentIteratorImpl),
              "zxio_dirent_iterator_t should match DirentIteratorImpl");

// C++ wrapper around zxio_remote_t.
class Remote {
 public:
  explicit Remote(zxio_t* io) : rio_(reinterpret_cast<zxio_remote_t*>(io)) {}

  [[nodiscard]] zx::unowned_channel control() const { return zx::unowned_channel(rio_->control); }

  [[nodiscard]] zx::unowned_handle event() const { return zx::unowned_handle(rio_->event); }

  [[nodiscard]] zx::unowned_stream stream() const { return zx::unowned_stream(rio_->stream); }

  zx::channel Release() {
    zx::channel control(rio_->control);
    rio_->control = ZX_HANDLE_INVALID;
    return control;
  }

  void Close() {
    Release().reset();
    if (rio_->event != ZX_HANDLE_INVALID) {
      zx_handle_close(rio_->event);
      rio_->event = ZX_HANDLE_INVALID;
    }
    if (rio_->stream != ZX_HANDLE_INVALID) {
      zx_handle_close(rio_->stream);
      rio_->stream = ZX_HANDLE_INVALID;
    }
  }

 private:
  zxio_remote_t* rio_;
};

zxio_node_protocols_t ToZxioNodeProtocols(uint32_t mode) {
  switch (mode & (S_IFMT | llcpp::fuchsia::io::MODE_TYPE_SERVICE)) {
    case S_IFDIR:
      return ZXIO_NODE_PROTOCOL_DIRECTORY;
    case S_IFCHR:
      return ZXIO_NODE_PROTOCOL_TTY;
    case S_IFBLK:
      return ZXIO_NODE_PROTOCOL_DEVICE;
    case S_IFREG:
      return ZXIO_NODE_PROTOCOL_FILE;
    case S_IFIFO:
      return ZXIO_NODE_PROTOCOL_PIPE;
      // fuchsia::io has mode type service which breaks stat.
      // TODO(fxbug.dev/52930): return ZXIO_NODE_PROTOCOL_CONNECTOR instead.
    case llcpp::fuchsia::io::MODE_TYPE_SERVICE:
      return ZXIO_NODE_PROTOCOL_FILE;
    case S_IFLNK:
      // Symbolic links are not supported on Fuchsia.
      // A reasonable fallback is to keep the protocols unchanged,
      // i.e. same as getting a protocol we do not understand.
      return ZXIO_NODE_PROTOCOL_NONE;
    case S_IFSOCK:
      return ZXIO_NODE_PROTOCOL_POSIX_SOCKET;
    default:
      return ZXIO_NODE_PROTOCOL_NONE;
  }
}

uint32_t ToIo1ModeFileType(zxio_node_protocols_t protocols) {
  // The "file type" portion of mode only allow one bit, so we find
  // the best approximation given some set of |protocols|, tie-breaking
  // in the following precedence.
  if (protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    return S_IFDIR;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_FILE) {
    return S_IFREG;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_MEMORY) {
    return S_IFREG;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_POSIX_SOCKET) {
    return S_IFSOCK;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_PIPE) {
    return S_IFIFO;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_DEVICE) {
    return S_IFBLK;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_TTY) {
    return S_IFCHR;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_DEBUGLOG) {
    return S_IFCHR;
  }
  if (protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    // There is no good analogue for FIDL services in POSIX land...
    // Returning "regular file" as a fallback.
    return S_IFREG;
  }
  return 0;
}

class ToZxioAbilitiesForFile {
 public:
  zxio_abilities_t operator()(uint32_t mode) {
    zxio_abilities_t abilities = ZXIO_OPERATION_NONE;
    if (mode & S_IRUSR) {
      abilities |= ZXIO_OPERATION_READ_BYTES;
    }
    if (mode & S_IWUSR) {
      abilities |= ZXIO_OPERATION_WRITE_BYTES;
    }
    if (mode & S_IXUSR) {
      abilities |= ZXIO_OPERATION_EXECUTE;
    }
    // In addition, POSIX seems to allow changing file metadata
    // regardless of read/write permissions, as long as we are the
    // owner.
    abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
    abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
    return abilities;
  }
};

class ToIo1ModePermissionsForFile {
 public:
  uint32_t operator()(zxio_abilities_t abilities) {
    // Permissions are not applicable on Fuchsia.
    // We could approximate them using the |abilities| of a node.
    uint32_t permission_bits = 0;
    if (abilities & ZXIO_OPERATION_READ_BYTES) {
      permission_bits |= S_IRUSR;
    }
    if (abilities & ZXIO_OPERATION_WRITE_BYTES) {
      permission_bits |= S_IWUSR;
    }
    if (abilities & ZXIO_OPERATION_EXECUTE) {
      permission_bits |= S_IXUSR;
    }
    return permission_bits;
  }
};

class ToZxioAbilitiesForDirectory {
 public:
  zxio_abilities_t operator()(uint32_t mode) {
    zxio_abilities_t abilities = ZXIO_OPERATION_NONE;
    if (mode & S_IRUSR) {
      abilities |= ZXIO_OPERATION_ENUMERATE;
    }
    if (mode & S_IWUSR) {
      abilities |= ZXIO_OPERATION_MODIFY_DIRECTORY;
    }
    if (mode & S_IXUSR) {
      abilities |= ZXIO_OPERATION_TRAVERSE;
    }
    // In addition, POSIX seems to allow changing file metadata
    // regardless of read/write permissions, as long as we are the
    // owner.
    abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
    abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
    return abilities;
  }
};

class ToIo1ModePermissionsForDirectory {
 public:
  uint32_t operator()(zxio_abilities_t abilities) {
    // Permissions are not applicable on Fuchsia.
    // We could approximate them using the |abilities| of a node.
    uint32_t permission_bits = 0;
    if (abilities & ZXIO_OPERATION_ENUMERATE) {
      permission_bits |= S_IRUSR;
    }
    if (abilities & ZXIO_OPERATION_MODIFY_DIRECTORY) {
      permission_bits |= S_IWUSR;
    }
    if (abilities & ZXIO_OPERATION_TRAVERSE) {
      permission_bits |= S_IXUSR;
    }
    return permission_bits;
  }
};

template <typename ToZxioAbilities>
zxio_node_attributes_t ToZxioNodeAttributes(fio::NodeAttributes attr, ToZxioAbilities to_zxio) {
  zxio_node_attributes_t zxio_attr = {};
  ZXIO_NODE_ATTR_SET(zxio_attr, protocols, ToZxioNodeProtocols(attr.mode));
  ZXIO_NODE_ATTR_SET(zxio_attr, abilities, to_zxio(attr.mode));
  ZXIO_NODE_ATTR_SET(zxio_attr, id, attr.id);
  ZXIO_NODE_ATTR_SET(zxio_attr, content_size, attr.content_size);
  ZXIO_NODE_ATTR_SET(zxio_attr, storage_size, attr.storage_size);
  ZXIO_NODE_ATTR_SET(zxio_attr, link_count, attr.link_count);
  ZXIO_NODE_ATTR_SET(zxio_attr, creation_time, attr.creation_time);
  ZXIO_NODE_ATTR_SET(zxio_attr, modification_time, attr.modification_time);
  return zxio_attr;
}

template <typename ToIo1ModePermissions>
fio::NodeAttributes ToNodeAttributes(zxio_node_attributes_t attr, ToIo1ModePermissions to_io1) {
  return fio::NodeAttributes{
      .mode = ToIo1ModeFileType(attr.protocols) | to_io1(attr.abilities),
      .id = attr.has.id ? attr.id : fio::INO_UNKNOWN,
      .content_size = attr.content_size,
      .storage_size = attr.storage_size,
      .link_count = attr.link_count,
      .creation_time = attr.creation_time,
      .modification_time = attr.modification_time,
  };
}

zx_status_t zxio_remote_close(zxio_t* io) {
  Remote rio(io);
  zx_status_t status = zxio_raw_remote_close(rio.control());
  rio.Close();
  return status;
}

zx_status_t zxio_remote_release(zxio_t* io, zx_handle_t* out_handle) {
  Remote rio(io);
  *out_handle = rio.Release().release();
  return ZX_OK;
}

zx_status_t zxio_remote_clone(zxio_t* io, zx_handle_t* out_handle) {
  Remote rio(io);
  return zxio_raw_remote_clone(rio.control(), out_handle);
}

void zxio_remote_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                            zx_signals_t* out_zx_signals) {
  Remote rio(io);
  *out_handle = rio.event()->get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= fio::DEVICE_SIGNAL_READABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_OUT_OF_BAND) {
    zx_signals |= fio::DEVICE_SIGNAL_OOB;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= fio::DEVICE_SIGNAL_WRITABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_ERROR) {
    zx_signals |= fio::DEVICE_SIGNAL_ERROR;
  }
  if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
    zx_signals |= fio::DEVICE_SIGNAL_HANGUP;
  }
  if (zxio_signals & ZXIO_SIGNAL_READ_DISABLED) {
    zx_signals |= ZX_CHANNEL_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

void zxio_remote_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & fio::DEVICE_SIGNAL_READABLE) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & fio::DEVICE_SIGNAL_OOB) {
    zxio_signals |= ZXIO_SIGNAL_OUT_OF_BAND;
  }
  if (zx_signals & fio::DEVICE_SIGNAL_WRITABLE) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  if (zx_signals & fio::DEVICE_SIGNAL_ERROR) {
    zxio_signals |= ZXIO_SIGNAL_ERROR;
  }
  if (zx_signals & fio::DEVICE_SIGNAL_HANGUP) {
    zxio_signals |= ZXIO_SIGNAL_PEER_CLOSED;
  }
  if (zx_signals & ZX_CHANNEL_PEER_CLOSED) {
    zxio_signals |= ZXIO_SIGNAL_READ_DISABLED;
  }
  *out_zxio_signals = zxio_signals;
}

zx_status_t zxio_remote_sync(zxio_t* io) {
  Remote rio(io);
  auto result = fio::Node::Call::Sync(rio.control());
  return result.ok() ? result.Unwrap()->s : result.status();
}

template <typename ToZxioAbilities>
zx_status_t zxio_common_attr_get(zx::unowned_channel control, ToZxioAbilities to_zxio,
                                 zxio_node_attributes_t* out_attr) {
  auto result = fio::Node::Call::GetAttr(std::move(control));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (auto status = result.Unwrap()->s; status != ZX_OK) {
    return status;
  }
  *out_attr = ToZxioNodeAttributes(result.Unwrap()->attributes, to_zxio);
  return ZX_OK;
}

template <typename ToIo1ModePermissions>
zx_status_t zxio_common_attr_set(zx::unowned_channel control, ToIo1ModePermissions to_io1,
                                 const zxio_node_attributes_t* attr) {
  uint32_t flags = 0;
  zxio_node_attributes_t::zxio_node_attr_has_t remaining = attr->has;
  if (attr->has.creation_time) {
    flags |= fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME;
    remaining.creation_time = false;
  }
  if (attr->has.modification_time) {
    flags |= fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME;
    remaining.modification_time = false;
  }
  zxio_node_attributes_t::zxio_node_attr_has_t all_absent = {};
  if (remaining != all_absent) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto result =
      fio::Node::Call::SetAttr(std::move(control), flags, ToNodeAttributes(*attr, to_io1));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  Remote rio(io);
  return zxio_common_attr_get(rio.control(), ToZxioAbilitiesForFile(), out_attr);
}

zx_status_t zxio_remote_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  Remote rio(io);
  return zxio_common_attr_set(rio.control(), ToIo1ModePermissionsForFile(), attr);
}

template <typename F>
static zx_status_t zxio_remote_do_vector(const Remote& rio, const zx_iovec_t* vector,
                                         size_t vector_count, zxio_flags_t flags,
                                         size_t* out_actual, F fn) {
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* data, size_t capacity, size_t* out_actual) {
                          auto buffer = static_cast<uint8_t*>(data);
                          size_t total = 0;
                          while (capacity > 0) {
                            size_t chunk = std::min(capacity, fio::MAX_BUF);
                            size_t actual;
                            zx_status_t status = fn(rio.control(), buffer, chunk, &actual);
                            if (status != ZX_OK) {
                              if (total > 0) {
                                break;
                              }
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

zx_status_t zxio_remote_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                              zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Remote rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->readv(0, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
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

zx_status_t zxio_remote_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                 size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Remote rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->readv_at(0, offset, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
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

zx_status_t zxio_remote_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                               zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Remote rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->writev(0, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::Buffer<fio::File::WriteRequest> request_buffer;
        fidl::Buffer<fio::File::WriteResponse> response_buffer;
        auto result = fio::File::Call::Write(std::move(control), request_buffer.view(),
                                             fidl::VectorView(fidl::unowned_ptr(buffer), capacity),
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
        *out_actual = actual;
        return ZX_OK;
      });
}

zx_status_t zxio_remote_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                  size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  Remote rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->writev_at(0, offset, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [&offset](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::Buffer<fio::File::WriteAtRequest> request_buffer;
        fidl::Buffer<fio::File::WriteAtResponse> response_buffer;
        auto result = fio::File::Call::WriteAt(
            std::move(control), request_buffer.view(),
            fidl::VectorView(fidl::unowned_ptr(buffer), capacity), offset, response_buffer.view());
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

zx_status_t zxio_remote_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                             size_t* out_offset) {
  Remote rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->seek(start, offset, out_offset);
  }

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
  llcpp::fuchsia::mem::Buffer* buffer = response->buffer.get();
  if (!buffer) {
    return ZX_ERR_IO;
  }
  if (buffer->vmo == ZX_HANDLE_INVALID) {
    return ZX_ERR_IO;
  }
  *out_vmo = buffer->vmo.release();
  if (out_size) {
    *out_size = buffer->size;
  }
  return ZX_OK;
}

zx_status_t zxio_remote_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                   size_t path_len, zx_handle_t request) {
  Remote rio(io);
  auto result = fio::Directory::Call::Open(rio.control(), flags, mode,
                                           fidl::unowned_str(path, path_len), zx::channel(request));
  return result.status();
}

zx_status_t zxio_remote_unlink(zxio_t* io, const char* path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Unlink(rio.control(), fidl::unowned_str(path, strlen(path)));
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
      rio.control(), fidl::unowned_str(src_path, strlen(src_path)), zx::handle(dst_token),
      fidl::unowned_str(dst_path, strlen(dst_path)));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_link(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                             const char* dst_path) {
  Remote rio(io);
  auto result = fio::Directory::Call::Link(
      rio.control(), fidl::unowned_str(src_path, strlen(src_path)), zx::handle(dst_token),
      fidl::unowned_str(dst_path, strlen(dst_path)));
  return result.ok() ? result.Unwrap()->s : result.status();
}

zx_status_t zxio_remote_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator) {
  new (iterator) DirentIteratorImpl(directory);
  return ZX_OK;
}

zx_status_t zxio_remote_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                             zxio_dirent_t** out_entry) {
  return reinterpret_cast<DirentIteratorImpl*>(iterator)->Next(out_entry);
}

void zxio_remote_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  reinterpret_cast<DirentIteratorImpl*>(iterator)->~DirentIteratorImpl();
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
  ops.wait_begin = zxio_remote_wait_begin;
  ops.wait_end = zxio_remote_wait_end;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_attr_get;
  ops.attr_set = zxio_remote_attr_set;
  ops.readv = zxio_remote_readv;
  ops.readv_at = zxio_remote_readv_at;
  ops.writev = zxio_remote_writev;
  ops.writev_at = zxio_remote_writev_at;
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
  ops.dirent_iterator_init = zxio_remote_dirent_iterator_init;
  ops.dirent_iterator_next = zxio_remote_dirent_iterator_next;
  ops.dirent_iterator_destroy = zxio_remote_dirent_iterator_destroy;
  ops.isatty = zxio_remote_isatty;
  return ops;
}();

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t event) {
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_remote_ops);
  remote->control = control;
  remote->event = event;
  remote->stream = ZX_HANDLE_INVALID;
  return ZX_OK;
}

namespace {

zx_status_t zxio_dir_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
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

zx_status_t zxio_dir_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                              size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  return zxio_dir_readv(io, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_dir_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  Remote rio(io);
  return zxio_common_attr_get(rio.control(), ToZxioAbilitiesForDirectory(), out_attr);
}

zx_status_t zxio_dir_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  Remote rio(io);
  return zxio_common_attr_set(rio.control(), ToIo1ModePermissionsForDirectory(), attr);
}

}  // namespace

static constexpr zxio_ops_t zxio_dir_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.clone = zxio_remote_clone;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_dir_attr_get;
  ops.attr_set = zxio_dir_attr_set;
  // use specialized read functions that succeed for zero-sized reads.
  ops.readv = zxio_dir_readv;
  ops.readv_at = zxio_dir_readv_at;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.open_async = zxio_remote_open_async;
  ops.unlink = zxio_remote_unlink;
  ops.token_get = zxio_remote_token_get;
  ops.rename = zxio_remote_rename;
  ops.link = zxio_remote_link;
  ops.dirent_iterator_init = zxio_remote_dirent_iterator_init;
  ops.dirent_iterator_next = zxio_remote_dirent_iterator_next;
  ops.dirent_iterator_destroy = zxio_remote_dirent_iterator_destroy;
  return ops;
}();

zx_status_t zxio_dir_init(zxio_storage_t* storage, zx_handle_t control) {
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_dir_ops);
  remote->control = control;
  remote->event = ZX_HANDLE_INVALID;
  remote->stream = ZX_HANDLE_INVALID;
  return ZX_OK;
}

namespace {

void zxio_file_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                          zx_signals_t* out_zx_signals) {
  Remote rio(io);
  *out_handle = rio.event()->get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= fio::FILE_SIGNAL_READABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= fio::FILE_SIGNAL_WRITABLE;
  }
  *out_zx_signals = zx_signals;
}

void zxio_file_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & fio::FILE_SIGNAL_READABLE) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & fio::FILE_SIGNAL_WRITABLE) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  *out_zxio_signals = zxio_signals;
}

zx_status_t zxio_file_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  Remote rio(io);
  return zxio_common_attr_get(rio.control(), ToZxioAbilitiesForFile(), out_attr);
}

zx_status_t zxio_file_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  Remote rio(io);
  return zxio_common_attr_set(rio.control(), ToIo1ModePermissionsForFile(), attr);
}

}  // namespace

static constexpr zxio_ops_t zxio_file_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.clone = zxio_remote_clone;
  ops.wait_begin = zxio_file_wait_begin;
  ops.wait_end = zxio_file_wait_end;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_file_attr_get;
  ops.attr_set = zxio_file_attr_set;
  ops.readv = zxio_remote_readv;
  ops.readv_at = zxio_remote_readv_at;
  ops.writev = zxio_remote_writev;
  ops.writev_at = zxio_remote_writev_at;
  ops.seek = zxio_remote_seek;
  ops.truncate = zxio_remote_truncate;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.vmo_get = zxio_remote_vmo_get;
  return ops;
}();

zx_status_t zxio_file_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t event,
                           zx_handle_t stream) {
  auto remote = reinterpret_cast<zxio_remote_t*>(storage);
  zxio_init(&remote->io, &zxio_file_ops);
  remote->control = control;
  remote->event = event;
  remote->stream = stream;
  return ZX_OK;
}

uint32_t zxio_node_protocols_to_posix_type(zxio_node_protocols_t protocols) {
  return ToIo1ModeFileType(protocols);
}

uint32_t zxio_abilities_to_posix_permissions_for_file(zxio_abilities_t abilities) {
  return ToIo1ModePermissionsForFile()(abilities);
}

uint32_t zxio_abilities_to_posix_permissions_for_directory(zxio_abilities_t abilities) {
  return ToIo1ModePermissionsForDirectory()(abilities);
}

zx_status_t zxio_raw_remote_close(zx::unowned_channel control) {
  auto result = fio::Node::Call::Close(std::move(control));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  return result.Unwrap()->s;
}

zx_status_t zxio_raw_remote_clone(zx::unowned_channel source, zx_handle_t* out_handle) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t flags = fio::CLONE_FLAG_SAME_RIGHTS;
  auto result = fio::Node::Call::Clone(std::move(source), flags, std::move(remote));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  *out_handle = local.release();
  return ZX_OK;
}

zx_status_t zxio_raw_remote_attr_get(zx::unowned_channel control,
                                     zxio_node_attributes_t* out_attr) {
  return zxio_common_attr_get(std::move(control), ToZxioAbilitiesForFile(), out_attr);
}

zx_status_t zxio_raw_remote_attr_set(zx::unowned_channel control,
                                     const zxio_node_attributes_t* attr) {
  return zxio_common_attr_set(std::move(control), ToIo1ModePermissionsForFile(), attr);
}

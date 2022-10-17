// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/posix_mode.h>
#include <lib/zxio/types.h>
#include <sys/stat.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include "private.h"

namespace fdevice = fuchsia_device;
namespace fio = fuchsia_io;

namespace {

using zxio_remote_t = struct zxio_remote {
  zxio_t io;
  bool is_tty;
  zx::event event;
  zx::stream stream;
  fidl::WireSyncClient<fio::Node> client;

  fidl::UnownedClientEnd<fio::File> as_file() const { return as<fio::File>(); }

  fidl::UnownedClientEnd<fio::Directory> as_directory() const { return as<fio::Directory>(); }

  fidl::UnownedClientEnd<fio::AdvisoryLocking> as_advisory_locking() const {
    return as<fio::AdvisoryLocking>();
  }

 private:
  template <typename T>
  fidl::UnownedClientEnd<T> as() const {
    return fidl::UnownedClientEnd<T>(client.client_end().borrow().channel());
  }
};

static_assert(sizeof(zxio_remote_t) <= sizeof(zxio_storage_t),
              "zxio_remote_t must fit inside zxio_storage_t.");

// Implementation of |zxio_dirent_iterator_t| for |fuchsia.io| v1.
class DirentIteratorImpl {
 public:
  explicit DirentIteratorImpl(zxio_t* io) : io_(reinterpret_cast<zxio_remote_t*>(io)) {
    static_assert(offsetof(DirentIteratorImpl, io_) == 0,
                  "zxio_dirent_iterator_t requires first field of implementation to be zxio_t");
  }

  ~DirentIteratorImpl() {
    __UNUSED const fidl::WireResult result = fidl::WireCall(io_->as_directory())->Rewind();
  }

  zx_status_t Next(zxio_dirent_t* inout_entry) {
    if (index_ >= count_) {
      const zx_status_t status = RemoteReadDirents();
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

    auto packed_entry = reinterpret_cast<const dirent*>(&data_[index_]);

    // Check if we can read the entry size.
    if (index_ + sizeof(dirent) > count_) {
      // Should not happen
      return ZX_ERR_INTERNAL;
    }

    const size_t packed_entry_size = sizeof(dirent) + packed_entry->size;

    // Check if we can read the whole entry.
    if (index_ + packed_entry_size > count_) {
      // Should not happen
      return ZX_ERR_INTERNAL;
    }

    // Check that the name length is within bounds.
    if (packed_entry->size > fio::wire::kMaxFilename) {
      return ZX_ERR_INVALID_ARGS;
    }

    index_ += packed_entry_size;

    ZXIO_DIRENT_SET(*inout_entry, protocols, DTypeToProtocols(packed_entry->type));
    ZXIO_DIRENT_SET(*inout_entry, id, packed_entry->ino);
    inout_entry->name_length = packed_entry->size;
    if (inout_entry->name != nullptr) {
      memcpy(inout_entry->name, packed_entry->name, packed_entry->size);
    }

    return ZX_OK;
  }

 private:
  zx_status_t RemoteReadDirents() {
    fidl::BufferSpan fidl_buffer(buffer_, sizeof(buffer_));
    const fidl::WireUnownedResult result =
        fidl::WireCall(io_->as_directory()).buffer(fidl_buffer)->ReadDirents(kBufferSize);
    if (!result.ok()) {
      return result.status();
    }
    const auto& response = result.value();
    if (const zx_status_t status = response.s; status != ZX_OK) {
      return status;
    }
    const fidl::VectorView dirents = response.dirents;
    if (dirents.count() > kBufferSize) {
      return ZX_ERR_IO;
    }
    data_ = dirents.data();
    count_ = dirents.count();
    return ZX_OK;
  }

  static zxio_node_protocols_t DTypeToProtocols(uint8_t type) {
    switch (type) {
      case DT_DIR:
        return ZXIO_NODE_PROTOCOL_DIRECTORY;
      case DT_REG:
        return ZXIO_NODE_PROTOCOL_FILE;
      case DT_BLK:
        // Not supported.
      case DT_CHR:
        // Not supported.
      case DT_FIFO:
        // Not supported.
      case DT_LNK:
        // Not supported.
      case DT_SOCK:
        // Not supported.
      default:
        return ZXIO_NODE_PROTOCOL_NONE;
    }
  }

  // The maximum buffer size that is supported by |fuchsia.io/Directory.ReadDirents|.
  static constexpr size_t kBufferSize = fio::wire::kMaxBuf;

  zxio_remote_t* io_;

  // Issuing a FIDL call requires storage for both the request (16 bytes) and the largest possible
  // response message (8192 bytes of payload).
  // TODO(https://fxbug.dev/85843): Once overlapping request and response is allowed, reduce
  // this allocation to a single channel message size.
  FIDL_ALIGNDECL uint8_t
      buffer_[fidl::SyncClientMethodBufferSizeInChannel<fio::Directory::ReadDirents>()];
  const uint8_t* data_ = nullptr;
  uint64_t count_ = 0;
  uint64_t index_ = 0;
};

static_assert(sizeof(DirentIteratorImpl) <= sizeof(zxio_dirent_iterator_t),
              "DirentIteratorImpl should fit within a zxio_dirent_iterator_t");

zxio_node_protocols_t ToZxioNodeProtocols(uint32_t mode) {
  switch (mode & (S_IFMT | fio::wire::kModeTypeService)) {
    case S_IFDIR:
      return ZXIO_NODE_PROTOCOL_DIRECTORY;
    case S_IFREG:
      return ZXIO_NODE_PROTOCOL_FILE;
    case fio::wire::kModeTypeService:
      // fuchsia::io has mode type service which breaks stat.
      // TODO(fxbug.dev/52930): return ZXIO_NODE_PROTOCOL_CONNECTOR instead.
      return ZXIO_NODE_PROTOCOL_FILE;
    case S_IFBLK:
      // Block-oriented devices are not supported on Fuchsia.
    case S_IFCHR:
      // Character-oriented devices are not supported on Fuchsia.
    case S_IFIFO:
      // Named pipes are not supported on Fuchsia.
    case S_IFLNK:
      // Symbolic links are not supported on Fuchsia.
    case S_IFSOCK:
      // Named sockets are not supported on Fuchsia.
    default:
      // A reasonable fallback is to keep the protocols unchanged,
      // i.e. same as getting a protocol we do not understand.
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
zxio_node_attributes_t ToZxioNodeAttributes(fio::wire::NodeAttributes attr,
                                            ToZxioAbilities to_zxio) {
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
fio::wire::NodeAttributes ToNodeAttributes(zxio_node_attributes_t attr,
                                           ToIo1ModePermissions to_io1) {
  return fio::wire::NodeAttributes{
      .mode = ToIo1ModeFileType(attr.protocols) | to_io1(attr.abilities),
      .id = attr.has.id ? attr.id : fio::wire::kInoUnknown,
      .content_size = attr.content_size,
      .storage_size = attr.storage_size,
      .link_count = attr.link_count,
      .creation_time = attr.creation_time,
      .modification_time = attr.modification_time,
  };
}

// POSIX expects EBADF for access denied errors which comes from ZX_ERR_BAD_STATE;
// ZX_ERR_ACCESS_DENIED produces EACCES which should only be used for sockets.
zx_status_t map_status(zx_status_t status) {
  switch (status) {
    case ZX_ERR_ACCESS_DENIED:
      return ZX_ERR_BAD_HANDLE;
  }
  return status;
}

zx_status_t zxio_remote_close(zxio_t* io) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const zx_status_t status = [&]() {
    if (remote.client.is_valid()) {
      const fidl::WireResult result = remote.client->Close();
      if (!result.ok()) {
        return result.status();
      }
      const auto& response = result.value();
      if (response.is_error()) {
        return response.error_value();
      }
    }
    return ZX_OK;
  }();
  remote.~zxio_remote_t();
  return status;
}

zx_status_t zxio_remote_release(zxio_t* io, zx_handle_t* out_handle) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  *out_handle = remote.client.TakeClientEnd().TakeChannel().release();
  return ZX_OK;
}

zx_status_t zxio_remote_borrow(zxio_t* io, zx_handle_t* out_handle) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  *out_handle = remote.client.client_end().channel().get();
  return ZX_OK;
}

zx_status_t zxio_remote_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  auto [client_end, server_end] = std::move(endpoints.value());
  const fidl::WireResult result =
      remote.client->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(server_end));
  if (!result.ok()) {
    return result.status();
  }
  *out_handle = client_end.TakeChannel().release();
  return ZX_OK;
}

void zxio_remote_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                            zx_signals_t* out_zx_signals) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  *out_handle = remote.event.get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  zx_signals |= [zxio_signals]() {
    fdevice::wire::DeviceSignal signals;
    if (zxio_signals & ZXIO_SIGNAL_READABLE) {
      signals |= fdevice::wire::DeviceSignal::kReadable;
    }
    if (zxio_signals & ZXIO_SIGNAL_OUT_OF_BAND) {
      signals |= fdevice::wire::DeviceSignal::kOob;
    }
    if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
      signals |= fdevice::wire::DeviceSignal::kWritable;
    }
    if (zxio_signals & ZXIO_SIGNAL_ERROR) {
      signals |= fdevice::wire::DeviceSignal::kError;
    }
    if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
      signals |= fdevice::wire::DeviceSignal::kHangup;
    }
    return static_cast<zx_signals_t>(signals);
  }();
  if (zxio_signals & ZXIO_SIGNAL_READ_DISABLED) {
    zx_signals |= ZX_CHANNEL_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

void zxio_remote_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  [&zxio_signals, signals = fdevice::wire::DeviceSignal::TruncatingUnknown(zx_signals)]() {
    if (signals & fdevice::wire::DeviceSignal::kReadable) {
      zxio_signals |= ZXIO_SIGNAL_READABLE;
    }
    if (signals & fdevice::wire::DeviceSignal::kOob) {
      zxio_signals |= ZXIO_SIGNAL_OUT_OF_BAND;
    }
    if (signals & fdevice::wire::DeviceSignal::kWritable) {
      zxio_signals |= ZXIO_SIGNAL_WRITABLE;
    }
    if (signals & fdevice::wire::DeviceSignal::kError) {
      zxio_signals |= ZXIO_SIGNAL_ERROR;
    }
    if (signals & fdevice::wire::DeviceSignal::kHangup) {
      zxio_signals |= ZXIO_SIGNAL_PEER_CLOSED;
    }
  }();
  if (zx_signals & ZX_CHANNEL_PEER_CLOSED) {
    zxio_signals |= ZXIO_SIGNAL_READ_DISABLED;
  }
  *out_zxio_signals = zxio_signals;
}

zx_status_t zxio_remote_sync(zxio_t* io) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result = remote.client->Sync();
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

template <typename ToZxioAbilities>
zx_status_t zxio_common_attr_get(const fidl::WireSyncClient<fio::Node>& client,
                                 ToZxioAbilities to_zxio, zxio_node_attributes_t* out_attr) {
  const fidl::WireResult result = client->GetAttr();
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (const zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }
  *out_attr = ToZxioNodeAttributes(response.attributes, to_zxio);
  return ZX_OK;
}

template <typename ToIo1ModePermissions>
zx_status_t zxio_common_attr_set(const fidl::WireSyncClient<fio::Node>& client,
                                 ToIo1ModePermissions to_io1, const zxio_node_attributes_t* attr) {
  fio::wire::NodeAttributeFlags flags;
  zxio_node_attributes_t::zxio_node_attr_has_t remaining = attr->has;
  if (attr->has.creation_time) {
    flags |= fio::wire::NodeAttributeFlags::kCreationTime;
    remaining.creation_time = false;
  }
  if (attr->has.modification_time) {
    flags |= fio::wire::NodeAttributeFlags::kModificationTime;
    remaining.modification_time = false;
  }
  constexpr zxio_node_attributes_t::zxio_node_attr_has_t all_absent = {};
  if (remaining != all_absent) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const fidl::WireResult result = client->SetAttr(flags, ToNodeAttributes(*attr, to_io1));
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  return response.s;
}

zx_status_t zxio_remote_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_get(remote.client, ToZxioAbilitiesForFile(), out_attr);
}

zx_status_t zxio_remote_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_set(remote.client, ToIo1ModePermissionsForFile(), attr);
}

zx_status_t zxio_common_advisory_lock(const fidl::UnownedClientEnd<fio::AdvisoryLocking>& client,
                                      advisory_lock_req* req) {
  fio::wire::AdvisoryLockType lock_type;
  switch (req->type) {
    case ADVISORY_LOCK_SHARED:
      lock_type = fio::wire::AdvisoryLockType::kRead;
      break;
    case ADVISORY_LOCK_EXCLUSIVE:
      lock_type = fio::wire::AdvisoryLockType::kWrite;
      break;
    case ADVISORY_LOCK_UNLOCK:
      lock_type = fio::wire::AdvisoryLockType::kUnlock;
      break;
    default:
      return ZX_ERR_INTERNAL;
  }
  fidl::Arena allocator;
  const fidl::WireResult result = fidl::WireCall(client)->AdvisoryLock(
      fio::wire::AdvisoryLockRequest::Builder(allocator).type(lock_type).wait(req->wait).Build());
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

template <typename F>
zx_status_t zxio_remote_do_vector(const zxio_remote_t& remote, const zx_iovec_t* vector,
                                  size_t vector_count, zxio_flags_t flags, size_t* out_actual,
                                  F fn) {
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* data, size_t capacity, size_t* out_actual) {
                          auto buffer = static_cast<uint8_t*>(data);
                          size_t total = 0;
                          while (capacity > 0) {
                            const size_t chunk = std::min(capacity, fio::wire::kMaxBuf);
                            size_t actual;
                            const zx_status_t status = fn(remote, buffer, chunk, &actual);
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

  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);

  if (remote.stream.is_valid()) {
    return map_status(remote.stream.readv(0, vector, vector_count, out_actual));
  }

  return zxio_remote_do_vector(
      remote, vector, vector_count, flags, out_actual,
      [](const zxio_remote_t& remote, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File::Read> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(remote.as_file()).buffer(fidl_buffer.view())->Read(capacity);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        if (response.is_error()) {
          return response.error_value();
        }
        const fidl::VectorView data = response.value()->data;
        const size_t actual = data.count();
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

  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);

  if (remote.stream.is_valid()) {
    return map_status(remote.stream.readv_at(0, offset, vector, vector_count, out_actual));
  }

  return zxio_remote_do_vector(
      remote, vector, vector_count, flags, out_actual,
      [&offset](const zxio_remote_t& remote, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File::ReadAt> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(remote.as_file()).buffer(fidl_buffer.view())->ReadAt(capacity, offset);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        if (response.is_error()) {
          return response.error_value();
        }
        const fidl::VectorView data = response.value()->data;
        const size_t actual = data.count();
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

  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);

  if (remote.stream.is_valid()) {
    return map_status(remote.stream.writev(0, vector, vector_count, out_actual));
  }

  return zxio_remote_do_vector(
      remote, vector, vector_count, flags, out_actual,
      [](const zxio_remote_t& remote, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File::Write> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(remote.as_file())
                .buffer(fidl_buffer.view())
                ->Write(fidl::VectorView<uint8_t>::FromExternal(buffer, capacity));
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        if (response.is_error()) {
          return response.error_value();
        }
        const size_t actual = response.value()->actual_count;
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

  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);

  if (remote.stream.is_valid()) {
    return map_status(remote.stream.writev_at(0, offset, vector, vector_count, out_actual));
  }

  return zxio_remote_do_vector(
      remote, vector, vector_count, flags, out_actual,
      [&offset](const zxio_remote_t& remote, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File::WriteAt> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(remote.as_file())
                .buffer(fidl_buffer.view())
                ->WriteAt(fidl::VectorView<uint8_t>::FromExternal(buffer, capacity), offset);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        if (response.is_error()) {
          return response.error_value();
        }
        const size_t actual = response.value()->actual_count;
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
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  if (remote.stream.is_valid()) {
    return map_status(remote.stream.seek(start, offset, out_offset));
  }

  const fidl::WireResult result =
      fidl::WireCall(remote.as_file())->Seek(static_cast<fio::wire::SeekOrigin>(start), offset);
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  *out_offset = response.value()->offset_from_start;
  return ZX_OK;
}

zx_status_t zxio_remote_truncate(zxio_t* io, uint64_t length) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result = fidl::WireCall(remote.as_file())->Resize(length);
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

zx_status_t zxio_remote_flags_get(zxio_t* io, uint32_t* out_flags) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result = fidl::WireCall(remote.as_file())->GetFlags();
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (const zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }
  *out_flags = static_cast<uint32_t>(response.flags);
  return ZX_OK;
}

zx_status_t zxio_remote_flags_set(zxio_t* io, uint32_t flags) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result =
      fidl::WireCall(remote.as_file())->SetFlags(static_cast<fio::wire::OpenFlags>(flags));
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  return response.s;
}

zx_status_t zxio_remote_vmo_get(zxio_t* io, zxio_vmo_flags_t zxio_flags, zx_handle_t* out_vmo) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  fio::wire::VmoFlags flags;
  if (zxio_flags & ZXIO_VMO_READ) {
    flags |= fio::wire::VmoFlags::kRead;
  }
  if (zxio_flags & ZXIO_VMO_WRITE) {
    flags |= fio::wire::VmoFlags::kWrite;
  }
  if (zxio_flags & ZXIO_VMO_EXECUTE) {
    flags |= fio::wire::VmoFlags::kExecute;
  }
  if (zxio_flags & ZXIO_VMO_PRIVATE_CLONE) {
    flags |= fio::wire::VmoFlags::kPrivateClone;
  }
  if (zxio_flags & ZXIO_VMO_SHARED_BUFFER) {
    flags |= fio::wire::VmoFlags::kSharedBuffer;
  }
  fidl::WireResult result = fidl::WireCall(remote.as_file())->GetBackingMemory(flags);
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  zx::vmo& vmo = response.value()->vmo;
  *out_vmo = vmo.release();
  return ZX_OK;
}

zx_status_t zxio_dir_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                          size_t path_len, zxio_storage_t* storage) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }
  auto [client_end, server_end] = std::move(endpoints.value());
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Open(static_cast<fio::wire::OpenFlags>(flags) | fio::wire::OpenFlags::kDescribe, mode,
                 fidl::StringView::FromExternal(path, path_len), std::move(server_end));
  if (!result.ok()) {
    return result.status();
  }
  return zxio_create_with_on_open(client_end.TakeChannel().release(), storage);
}

zx_status_t zxio_remote_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                   size_t path_len, zx_handle_t request) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  fidl::ServerEnd<fio::Node> node_request{zx::channel(request)};
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Open(static_cast<fio::wire::OpenFlags>(flags), mode,
                 fidl::StringView::FromExternal(path, path_len), std::move(node_request));
  return result.status();
}

zx_status_t zxio_remote_add_inotify_filter(zxio_t* io, const char* path, size_t path_len,
                                           uint32_t mask, uint32_t watch_descriptor,
                                           zx_handle_t socket_handle) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const auto inotify_mask = static_cast<fio::wire::InotifyWatchMask>(mask);
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->AddInotifyFilter(fidl::StringView::FromExternal(path, path_len), inotify_mask,
                             watch_descriptor, zx::socket(socket_handle));
  return result.status();
}

zx_status_t zxio_remote_unlink(zxio_t* io, const char* name, size_t name_len, int flags) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  fidl::Arena allocator;
  auto options = fio::wire::UnlinkOptions::Builder(allocator);
  auto io_flags = fio::wire::UnlinkFlags::kMustBeDirectory;
  if (flags & AT_REMOVEDIR) {
    options.flags(fidl::ObjectView<decltype(io_flags)>::FromExternal(&io_flags));
  }
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Unlink(fidl::StringView::FromExternal(name, name_len), options.Build());
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

zx_status_t zxio_remote_token_get(zxio_t* io, zx_handle_t* out_token) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  fidl::WireResult result = fidl::WireCall(remote.as_directory())->GetToken();
  if (!result.ok()) {
    return result.status();
  }
  auto& response = result.value();
  if (const zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }
  *out_token = response.token.release();
  return ZX_OK;
}

zx_status_t zxio_remote_rename(zxio_t* io, const char* old_path, size_t old_path_len,
                               zx_handle_t dst_token, const char* new_path, size_t new_path_len) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Rename(fidl::StringView::FromExternal(old_path, old_path_len), zx::event(dst_token),
                   fidl::StringView::FromExternal(new_path, new_path_len));
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (response.is_error()) {
    return response.error_value();
  }
  return ZX_OK;
}

zx_status_t zxio_remote_link(zxio_t* io, const char* src_path, size_t src_path_len,
                             zx_handle_t dst_token, const char* dst_path, size_t dst_path_len) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Link(fidl::StringView::FromExternal(src_path, src_path_len), zx::handle(dst_token),
                 fidl::StringView::FromExternal(dst_path, dst_path_len));
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  return response.s;
}

zx_status_t zxio_remote_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator) {
  new (iterator) DirentIteratorImpl(directory);
  return ZX_OK;
}

zx_status_t zxio_remote_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                             zxio_dirent_t* inout_entry) {
  return reinterpret_cast<DirentIteratorImpl*>(iterator)->Next(inout_entry);
}

void zxio_remote_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  reinterpret_cast<DirentIteratorImpl*>(iterator)->~DirentIteratorImpl();
}

zx_status_t zxio_remote_isatty(zxio_t* io, bool* tty) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  *tty = remote.is_tty;
  return ZX_OK;
}

zx_status_t zxio_remote_get_window_size(zxio_t* io, uint32_t* width, uint32_t* height) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  if (!remote.is_tty) {
    // Not a tty.
    return ZX_ERR_NOT_SUPPORTED;
  }
  const fidl::UnownedClientEnd<fuchsia_hardware_pty::Device> device(
      remote.client.client_end().borrow().channel());
  if (!device.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  const fidl::WireResult result = fidl::WireCall(device)->GetWindowSize();
  if (!result.ok()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *width = response.size.width;
  *height = response.size.height;
  return ZX_OK;
}

zx_status_t zxio_remote_set_window_size(zxio_t* io, uint32_t width, uint32_t height) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  if (!remote.is_tty) {
    // Not a tty.
    return ZX_ERR_NOT_SUPPORTED;
  }
  const fidl::UnownedClientEnd<fuchsia_hardware_pty::Device> device(
      remote.client.client_end().borrow().channel());
  if (!device.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  const fuchsia_hardware_pty::wire::WindowSize size = {
      .width = width,
      .height = height,
  };

  const fidl::WireResult result = fidl::WireCall(device)->SetWindowSize(size);
  if (!result.ok()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const auto& response = result.value();
  if (response.status != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace

static constexpr zxio_ops_t zxio_remote_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.borrow = zxio_remote_borrow;
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
  ops.add_inotify_filter = zxio_remote_add_inotify_filter;
  ops.unlink = zxio_remote_unlink;
  ops.token_get = zxio_remote_token_get;
  ops.rename = zxio_remote_rename;
  ops.link = zxio_remote_link;
  ops.dirent_iterator_init = zxio_remote_dirent_iterator_init;
  ops.dirent_iterator_next = zxio_remote_dirent_iterator_next;
  ops.dirent_iterator_destroy = zxio_remote_dirent_iterator_destroy;
  ops.isatty = zxio_remote_isatty;
  ops.get_window_size = zxio_remote_get_window_size;
  ops.set_window_size = zxio_remote_set_window_size;
  return ops;
}();

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx::event event,
                             fidl::ClientEnd<fio::Node> client, bool is_tty) {
  auto& remote = *new (storage) zxio_remote_t{
      .io = storage->io,
      .is_tty = is_tty,
      .event = std::move(event),
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&remote.io, &zxio_remote_ops);
  return ZX_OK;
}

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx::eventpair event,
                             fidl::ClientEnd<fio::Node> client, bool is_tty) {
  return zxio_remote_init(storage, zx::event{event.release()}, std::move(client), is_tty);
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
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_get(remote.client, ToZxioAbilitiesForDirectory(), out_attr);
}

zx_status_t zxio_dir_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_set(remote.client, ToIo1ModePermissionsForDirectory(), attr);
}

zx_status_t zxio_remote_advisory_lock(zxio_t* io, advisory_lock_req* req) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_advisory_lock(remote.as_advisory_locking(), req);
}

zx_status_t zxio_remote_watch_directory(zxio_t* io, zxio_watch_directory_cb cb, zx_time_t deadline,
                                        void* context) {
  if (cb == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  zx::result endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  const fidl::WireResult result =
      fidl::WireCall(remote.as_directory())
          ->Watch(fio::wire::WatchMask::kMask, 0, std::move(endpoints->server));

  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  if (const zx_status_t status = response.s; status != ZX_OK) {
    return status;
  }

  for (;;) {
    uint8_t bytes[fio::wire::kMaxBuf + 1];  // Extra byte for temporary null terminator.
    uint32_t num_bytes;
    zx_status_t status = endpoints->client.channel().read_etc(0, &bytes, nullptr, sizeof(bytes), 0,
                                                              &num_bytes, nullptr);
    if (status != ZX_OK) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        status = endpoints->client.channel().wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                      zx::time(deadline), nullptr);
        if (status != ZX_OK) {
          return status;
        }
        continue;
      }
      return status;
    }

    // Message Format: { OP, LEN, DATA[LEN] }
    const cpp20::span span(bytes, num_bytes);
    auto it = span.begin();
    for (;;) {
      if (std::distance(it, span.end()) < 2) {
        break;
      }

      const fio::wire::WatchEvent wire_event = static_cast<fio::wire::WatchEvent>(*it++);
      const uint8_t len = *it++;
      uint8_t* name = it;

      if (std::distance(it, span.end()) < len) {
        break;
      }
      it += len;

      zxio_watch_directory_event_t event;
      switch (wire_event) {
        case fio::wire::WatchEvent::kAdded:
        case fio::wire::WatchEvent::kExisting:
          event = ZXIO_WATCH_EVENT_ADD_FILE;
          break;
        case fio::wire::WatchEvent::kRemoved:
          event = ZXIO_WATCH_EVENT_REMOVE_FILE;
          break;
        case fio::wire::WatchEvent::kIdle:
          event = ZXIO_WATCH_EVENT_WAITING;
          break;
        default:
          // unsupported event
          continue;
      }

      // The callback expects a null-terminated string.
      const uint8_t tmp = *it;
      *it = 0;
      status = cb(event, reinterpret_cast<const char*>(name), context);
      *it = tmp;
      if (status != ZX_OK) {
        return status;
      }
    }
  }
}

}  // namespace

static constexpr zxio_ops_t zxio_dir_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.borrow = zxio_remote_borrow;
  ops.clone = zxio_remote_clone;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_dir_attr_get;
  ops.attr_set = zxio_dir_attr_set;
  // use specialized read functions that succeed for zero-sized reads.
  ops.readv = zxio_dir_readv;
  ops.readv_at = zxio_dir_readv_at;
  ops.flags_get = zxio_remote_flags_get;
  ops.flags_set = zxio_remote_flags_set;
  ops.open = zxio_dir_open;
  ops.open_async = zxio_remote_open_async;
  ops.add_inotify_filter = zxio_remote_add_inotify_filter;
  ops.unlink = zxio_remote_unlink;
  ops.token_get = zxio_remote_token_get;
  ops.rename = zxio_remote_rename;
  ops.link = zxio_remote_link;
  ops.dirent_iterator_init = zxio_remote_dirent_iterator_init;
  ops.dirent_iterator_next = zxio_remote_dirent_iterator_next;
  ops.dirent_iterator_destroy = zxio_remote_dirent_iterator_destroy;
  ops.advisory_lock = zxio_remote_advisory_lock;
  ops.watch_directory = zxio_remote_watch_directory;
  return ops;
}();

zx_status_t zxio_dir_init(zxio_storage_t* storage, fidl::ClientEnd<fio::Node> client) {
  auto& remote = *new (storage) zxio_remote_t{
      .io = storage->io,
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&remote.io, &zxio_dir_ops);
  return ZX_OK;
}

namespace {

void zxio_file_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                          zx_signals_t* out_zx_signals) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  *out_handle = remote.event.get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= static_cast<zx_signals_t>(fio::wire::FileSignal::kReadable);
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= static_cast<zx_signals_t>(fio::wire::FileSignal::kWritable);
  }
  *out_zx_signals = zx_signals;
}

void zxio_file_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & static_cast<zx_signals_t>(fio::wire::FileSignal::kReadable)) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & static_cast<zx_signals_t>(fio::wire::FileSignal::kWritable)) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  *out_zxio_signals = zxio_signals;
}

zx_status_t zxio_file_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_get(remote.client, ToZxioAbilitiesForFile(), out_attr);
}

zx_status_t zxio_file_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  auto& remote = *reinterpret_cast<zxio_remote_t*>(io);
  return zxio_common_attr_set(remote.client, ToIo1ModePermissionsForFile(), attr);
}

}  // namespace

static constexpr zxio_ops_t zxio_file_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_close;
  ops.release = zxio_remote_release;
  ops.borrow = zxio_remote_borrow;
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
  ops.advisory_lock = zxio_remote_advisory_lock;
  return ops;
}();

zx_status_t zxio_file_init(zxio_storage_t* storage, zx::event event, zx::stream stream,
                           fidl::ClientEnd<fuchsia_io::Node> client) {
  auto& remote = *new (storage) zxio_remote_t{
      .io = storage->io,
      .event = std::move(event),
      .stream = std::move(stream),
      .client = fidl::WireSyncClient(std::move(client)),
  };
  zxio_init(&remote.io, &zxio_file_ops);
  return ZX_OK;
}

uint32_t zxio_node_protocols_to_posix_type(zxio_node_protocols_t protocols) {
  return ToIo1ModeFileType(protocols);
}

__EXPORT
uint32_t zxio_get_posix_mode(zxio_node_protocols_t protocols, zxio_abilities_t abilities) {
  uint32_t mode = zxio_node_protocols_to_posix_type(protocols);
  if (mode & S_IFDIR) {
    mode |= ToIo1ModePermissionsForDirectory()(abilities);
  } else {
    mode |= ToIo1ModePermissionsForFile()(abilities);
  }
  return mode;
}

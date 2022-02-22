// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_v2.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include "common_utils.h"
#include "dirent_iterator.h"

namespace fio = fuchsia_io;

namespace {

zxio_node_attributes_t ToZxioNodeAttributes(const fio::wire::NodeAttributes2& attr) {
  zxio_node_attributes_t zxio_attr = {};
  if (attr.has_protocols()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, protocols, ToZxioNodeProtocols(attr.protocols()));
  }
  if (attr.has_abilities()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, protocols, ToZxioAbilities(attr.abilities()));
  }
  if (attr.has_id()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, id, attr.id());
  }
  if (attr.has_content_size()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, content_size, attr.content_size());
  }
  if (attr.has_storage_size()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, storage_size, attr.storage_size());
  }
  if (attr.has_link_count()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, link_count, attr.link_count());
  }
  if (attr.has_creation_time()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, creation_time, attr.creation_time());
  }
  if (attr.has_modification_time()) {
    ZXIO_NODE_ATTR_SET(zxio_attr, modification_time, attr.modification_time());
  }
  return zxio_attr;
}

fio::wire::NodeAttributes2 ToIo2NodeAttributes(fidl::AnyArena& allocator,
                                               const zxio_node_attributes_t& attr) {
  fio::wire::NodeAttributes2 node_attributes(allocator);
  if (attr.has.protocols) {
    node_attributes.set_protocols(allocator, ToIo2NodeProtocols(attr.protocols));
  }
  if (attr.has.abilities) {
    node_attributes.set_abilities(allocator, ToIo2Abilities(attr.abilities));
  }
  if (attr.has.id) {
    node_attributes.set_id(allocator, attr.id);
  }
  if (attr.has.content_size) {
    node_attributes.set_content_size(allocator, attr.content_size);
  }
  if (attr.has.storage_size) {
    node_attributes.set_storage_size(allocator, attr.storage_size);
  }
  if (attr.has.link_count) {
    node_attributes.set_link_count(allocator, attr.link_count);
  }
  if (attr.has.creation_time) {
    node_attributes.set_creation_time(allocator, attr.creation_time);
  }
  if (attr.has.modification_time) {
    node_attributes.set_modification_time(allocator, attr.modification_time);
  }
  return node_attributes;
}

// These functions are named with "v2" to avoid mixing up with fuchsia.io v1
// backend during grepping.

zx_status_t zxio_remote_v2_close(zxio_t* io) {
  RemoteV2 rio(io);
  zx_status_t status = [&]() {
    const fidl::WireResult result =
        fidl::WireCall(fidl::UnownedClientEnd<fio::Node2>(rio.control()))->Close();
    if (!result.ok()) {
      return result.status();
    }
    const auto& response = result.value();
    switch (response.result.Which()) {
      case fio::wire::Node2CloseResult::Tag::kErr:
        return response.result.err();
      case fio::wire::Node2CloseResult::Tag::kResponse:
        return ZX_OK;
    }
    return rio.control()->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
  }();
  rio.Close();
  return status;
}

zx_status_t zxio_remote_v2_release(zxio_t* io, zx_handle_t* out_handle) {
  RemoteV2 rio(io);
  *out_handle = rio.Release().release();
  return ZX_OK;
}

zx_status_t zxio_remote_v2_reopen(zxio_t* io, zxio_reopen_flags_t flags, zx_handle_t* out_handle) {
  RemoteV2 rio(io);
  zx::status ends = fidl::CreateEndpoints<fio::Node2>();
  if (ends.is_error()) {
    return ends.status_value();
  }
  fio::wire::ConnectionOptions options;
  if (flags & ZXIO_REOPEN_DESCRIBE) {
    options.flags() |= fio::wire::ConnectionFlags::kGetConnectionInfo;
  }
  const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fio::Node2>(rio.control()))
                                      ->Reopen(options, ends->server.TakeChannel());
  if (!result.ok()) {
    return result.status();
  }
  *out_handle = ends->client.TakeChannel().release();
  return ZX_OK;
}

void zxio_remote_v2_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                               zx_signals_t* out_zx_signals) {
  RemoteV2 rio(io);
  *out_handle = rio.observer()->get();
  using DeviceSignal = fio::wire::DeviceSignal2;
  auto device_signal_part = DeviceSignal();
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    device_signal_part |= DeviceSignal::kReadable;
  }
  if (zxio_signals & ZXIO_SIGNAL_OUT_OF_BAND) {
    device_signal_part |= DeviceSignal::kOob;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    device_signal_part |= DeviceSignal::kWritable;
  }
  if (zxio_signals & ZXIO_SIGNAL_ERROR) {
    device_signal_part |= DeviceSignal::kError;
  }
  if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
    device_signal_part |= DeviceSignal::kHangup;
  }
  // static_cast is a-okay, because |DeviceSignal| values are defined
  // using Zircon ZX_USER_* signals.
  auto zx_signals = static_cast<zx_signals_t>(device_signal_part);
  if (zxio_signals & ZXIO_SIGNAL_READ_DISABLED) {
    zx_signals |= ZX_CHANNEL_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

void zxio_remote_v2_wait_end(zxio_t* io, zx_signals_t zx_signals,
                             zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  using DeviceSignal = fio::wire::DeviceSignal2;
  // static_cast is a-okay, because |DeviceSignal| values are defined
  // using Zircon ZX_USER_* signals.
  auto device_signal_part = DeviceSignal::TruncatingUnknown(zx_signals);
  if (device_signal_part & DeviceSignal::kReadable) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (device_signal_part & DeviceSignal::kOob) {
    zxio_signals |= ZXIO_SIGNAL_OUT_OF_BAND;
  }
  if (device_signal_part & DeviceSignal::kWritable) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  if (device_signal_part & DeviceSignal::kError) {
    zxio_signals |= ZXIO_SIGNAL_ERROR;
  }
  if (device_signal_part & DeviceSignal::kHangup) {
    zxio_signals |= ZXIO_SIGNAL_PEER_CLOSED;
  }
  if (zx_signals & ZX_CHANNEL_PEER_CLOSED) {
    zxio_signals |= ZXIO_SIGNAL_READ_DISABLED;
  }
  *out_zxio_signals = zxio_signals;
}

zx_status_t zxio_remote_sync(zxio_t* io) {
  RemoteV2 rio(io);
  const fidl::WireResult result =
      fidl::WireCall(fidl::UnownedClientEnd<fio::Node2>(rio.control()))->Sync();
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  switch (response.result.Which()) {
    case fio::wire::Node2SyncResult::Tag::kErr:
      return response.result.err();
    case fio::wire::Node2SyncResult::Tag::kResponse:
      return ZX_OK;
  }
}

zx_status_t zxio_remote_v2_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  RemoteV2 rio(io);
  const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fio::Node2>(rio.control()))
                                      ->GetAttributes(fio::wire::NodeAttributesQuery::kMask);
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  switch (response.result.Which()) {
    case fio::wire::Node2GetAttributesResult::Tag::kErr:
      return response.result.err();
    case fio::wire::Node2GetAttributesResult::Tag::kResponse:
      const fio::wire::NodeAttributes2& attributes = response.result.response().attributes;
      *out_attr = ToZxioNodeAttributes(attributes);
      return ZX_OK;
  }
}

zx_status_t zxio_remote_v2_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  fidl::Arena<1024> allocator;
  RemoteV2 rio(io);
  const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fio::Node2>(rio.control()))
                                      ->UpdateAttributes(ToIo2NodeAttributes(allocator, *attr));
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  switch (response.result.Which()) {
    case fio::wire::Node2UpdateAttributesResult::Tag::kErr:
      return response.result.err();
    case fio::wire::Node2UpdateAttributesResult::Tag::kResponse:
      return ZX_OK;
  }
}

}  // namespace

void RemoteV2::Close() {
  Release().reset();
  if (rio_->observer != ZX_HANDLE_INVALID) {
    zx_handle_close(rio_->observer);
    rio_->observer = ZX_HANDLE_INVALID;
  }
  if (rio_->stream != ZX_HANDLE_INVALID) {
    zx_handle_close(rio_->stream);
    rio_->stream = ZX_HANDLE_INVALID;
  }
}

zx::channel RemoteV2::Release() {
  zx::channel control(rio_->control);
  rio_->control = ZX_HANDLE_INVALID;
  return control;
}

static constexpr zxio_ops_t zxio_remote_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.release = zxio_remote_v2_release;
  ops.reopen = zxio_remote_v2_reopen;
  ops.wait_begin = zxio_remote_v2_wait_begin;
  ops.wait_end = zxio_remote_v2_wait_end;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  return ops;
}();

zx_status_t zxio_remote_v2_init(zxio_storage_t* storage, zx_handle_t control,
                                zx_handle_t observer) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_remote_v2_ops);
  remote->control = control;
  remote->observer = observer;
  remote->stream = ZX_HANDLE_INVALID;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_dir_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.release = zxio_remote_v2_release;
  ops.reopen = zxio_remote_v2_reopen;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  ops.dirent_iterator_init = zxio_remote_v2_dirent_iterator_init;
  ops.dirent_iterator_next = zxio_remote_v2_dirent_iterator_next;
  ops.dirent_iterator_destroy = zxio_remote_v2_dirent_iterator_destroy;
  return ops;
}();

zx_status_t zxio_dir_v2_init(zxio_storage_t* storage, zx_handle_t control) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_dir_v2_ops);
  remote->control = control;
  remote->observer = ZX_HANDLE_INVALID;
  remote->stream = ZX_HANDLE_INVALID;
  return ZX_OK;
}

namespace {

void zxio_file_v2_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                             zx_signals_t* out_zx_signals) {
  using fio::wire::FileSignal;
  RemoteV2 rio(io);
  *out_handle = rio.observer()->get();
  auto file_signal_part = FileSignal();
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    file_signal_part |= FileSignal::kReadable;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    file_signal_part |= FileSignal::kWritable;
  }
  auto zx_signals = static_cast<zx_signals_t>(file_signal_part);
  *out_zx_signals = zx_signals;
}

void zxio_file_v2_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  using fio::wire::FileSignal;
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  auto file_signal_part = FileSignal::TruncatingUnknown(zx_signals);
  if (file_signal_part & FileSignal::kReadable) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (file_signal_part & FileSignal::kWritable) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  *out_zxio_signals = zxio_signals;
}

template <typename F>
static zx_status_t zxio_remote_do_vector(const RemoteV2& rio, const zx_iovec_t* vector,
                                         size_t vector_count, zxio_flags_t flags,
                                         size_t* out_actual, F fn) {
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* data, size_t capacity, size_t* out_actual) {
                          auto buffer = static_cast<uint8_t*>(data);
                          size_t total = 0;
                          while (capacity > 0) {
                            size_t chunk = std::min(capacity, fio::wire::kMaxTransferSize);
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

zx_status_t zxio_remote_v2_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                 zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  RemoteV2 rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->readv(0, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File2::Read> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(fidl::UnownedClientEnd<fio::File2>(control))
                .buffer(fidl_buffer.view())
                ->Read(capacity);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        switch (response.result.Which()) {
          case fio::wire::File2ReadResult::Tag::kErr:
            return response.result.err();
          case fio::wire::File2ReadResult::Tag::kResponse:
            const fidl::VectorView data = response.result.response().data;
            const size_t actual = data.count();
            if (actual > capacity) {
              return ZX_ERR_IO;
            }
            memcpy(buffer, data.begin(), actual);
            *out_actual = actual;
            return ZX_OK;
        }
      });
}

zx_status_t zxio_remote_v2_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                    size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  RemoteV2 rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->readv_at(0, offset, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [&offset](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        fidl::SyncClientBuffer<fio::File2::ReadAt> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(fidl::UnownedClientEnd<fio::File2>(control))
                .buffer(fidl_buffer.view())
                ->ReadAt(capacity, offset);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        switch (response.result.Which()) {
          case fio::wire::File2ReadAtResult::Tag::kErr:
            return response.result.err();
          case fio::wire::File2ReadAtResult::Tag::kResponse:
            const fidl::VectorView data = response.result.response().data;
            const size_t actual = data.count();
            if (actual > capacity) {
              return ZX_ERR_IO;
            }
            offset += actual;
            memcpy(buffer, data.begin(), actual);
            *out_actual = actual;
            return ZX_OK;
        }
      });
}

zx_status_t zxio_remote_v2_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                  zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  RemoteV2 rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->writev(0, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File2::Write> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(fidl::UnownedClientEnd<fio::File2>(control))
                .buffer(fidl_buffer.view())
                ->Write(fidl::VectorView<uint8_t>::FromExternal(buffer, capacity));
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        switch (response.result.Which()) {
          case fio::wire::File2WriteResult::Tag::kErr:
            return response.result.err();
          case fio::wire::File2WriteResult::Tag::kResponse:
            const size_t actual = response.result.response().actual_count;
            if (actual > capacity) {
              return ZX_ERR_IO;
            }
            *out_actual = actual;
            return ZX_OK;
        }
      });
}

zx_status_t zxio_remote_v2_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                     size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  RemoteV2 rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->writev_at(0, offset, vector, vector_count, out_actual);
  }

  return zxio_remote_do_vector(
      rio, vector, vector_count, flags, out_actual,
      [&offset](zx::unowned_channel control, uint8_t* buffer, size_t capacity, size_t* out_actual) {
        // Explicitly allocating message buffers to avoid heap allocation.
        fidl::SyncClientBuffer<fio::File2::WriteAt> fidl_buffer;
        const fidl::WireUnownedResult result =
            fidl::WireCall(fidl::UnownedClientEnd<fio::File2>(control))
                .buffer(fidl_buffer.view())
                ->WriteAt(fidl::VectorView<uint8_t>::FromExternal(buffer, capacity), offset);
        if (!result.ok()) {
          return result.status();
        }
        const auto& response = result.value();
        switch (response.result.Which()) {
          case fio::wire::File2WriteAtResult::Tag::kErr:
            return response.result.err();
          case fio::wire::File2WriteAtResult::Tag::kResponse:
            const size_t actual = response.result.response().actual_count;
            if (actual > capacity) {
              return ZX_ERR_IO;
            }
            offset += actual;
            *out_actual = actual;
            return ZX_OK;
        }
      });
}

zx_status_t zxio_remote_v2_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                                size_t* out_offset) {
  RemoteV2 rio(io);
  if (rio.stream()->is_valid()) {
    return rio.stream()->seek(start, offset, out_offset);
  }

  const fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fio::File2>(rio.control()))
                                      ->Seek(static_cast<fio::wire::SeekOrigin>(start), offset);
  if (!result.ok()) {
    return result.status();
  }
  const auto& response = result.value();
  switch (response.result.Which()) {
    case fio::wire::File2SeekResult::Tag::kErr:
      return response.result.err();
    case fio::wire::File2SeekResult::Tag::kResponse:
      *out_offset = response.result.response().offset_from_start;
      return ZX_OK;
  }
}

}  // namespace

static constexpr zxio_ops_t zxio_file_v2_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_remote_v2_close;
  ops.release = zxio_remote_v2_release;
  ops.reopen = zxio_remote_v2_reopen;
  ops.wait_begin = zxio_file_v2_wait_begin;
  ops.wait_end = zxio_file_v2_wait_end;
  ops.sync = zxio_remote_sync;
  ops.attr_get = zxio_remote_v2_attr_get;
  ops.attr_set = zxio_remote_v2_attr_set;
  ops.readv = zxio_remote_v2_readv;
  ops.readv_at = zxio_remote_v2_readv_at;
  ops.writev = zxio_remote_v2_writev;
  ops.writev_at = zxio_remote_v2_writev_at;
  ops.seek = zxio_remote_v2_seek;
  return ops;
}();

zx_status_t zxio_file_v2_init(zxio_storage_t* storage, zx_handle_t control, zx_handle_t observer,
                              zx_handle_t stream) {
  auto remote = reinterpret_cast<zxio_remote_v2_t*>(storage);
  zxio_init(&remote->io, &zxio_file_v2_ops);
  remote->control = control;
  remote->observer = observer;
  remote->stream = stream;
  return ZX_OK;
}

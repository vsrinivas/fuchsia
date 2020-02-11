// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/internal/file_connection.h>

#include <utility>

namespace vfs {
namespace internal {

FileConnection::FileConnection(uint32_t flags, vfs::internal::File* vn)
    : Connection(flags), vn_(vn), binding_(this) {}

FileConnection::~FileConnection() = default;

zx_status_t FileConnection::BindInternal(zx::channel request, async_dispatcher_t* dispatcher) {
  if (binding_.is_bound()) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = binding_.Bind(std::move(request), dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  binding_.set_error_handler([this](zx_status_t status) { vn_->Close(this); });
  return ZX_OK;
}

void FileConnection::Clone(uint32_t flags, fidl::InterfaceRequest<fuchsia::io::Node> object) {
  Connection::Clone(vn_, flags, object.TakeChannel(), binding_.dispatcher());
}

void FileConnection::Close(CloseCallback callback) { Connection::Close(vn_, std::move(callback)); }

void FileConnection::Describe(DescribeCallback callback) {
  Connection::Describe(vn_, std::move(callback));
}

void FileConnection::Sync(SyncCallback callback) { Connection::Sync(vn_, std::move(callback)); }

void FileConnection::GetAttr(GetAttrCallback callback) {
  Connection::GetAttr(vn_, std::move(callback));
}

void FileConnection::SetAttr(uint32_t flags, fuchsia::io::NodeAttributes attributes,
                             SetAttrCallback callback) {
  Connection::SetAttr(vn_, flags, attributes, std::move(callback));
}

void FileConnection::Read(uint64_t count, ReadCallback callback) {
  std::vector<uint8_t> data;
  if (!Flags::IsReadable(flags())) {
    callback(ZX_ERR_ACCESS_DENIED, std::move(data));
    return;
  }
  zx_status_t status = vn_->ReadAt(count, offset(), &data);
  if (status == ZX_OK) {
    set_offset(offset() + data.size());
  }
  callback(status, std::move(data));
}

void FileConnection::ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) {
  std::vector<uint8_t> data;
  if (!Flags::IsReadable(flags())) {
    callback(ZX_ERR_ACCESS_DENIED, std::move(data));
    return;
  }
  zx_status_t status = vn_->ReadAt(count, offset, &data);
  callback(status, std::move(data));
}

void FileConnection::Write(std::vector<uint8_t> data, WriteCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(ZX_ERR_ACCESS_DENIED, 0);
    return;
  }
  uint64_t actual = 0u;
  zx_status_t status = vn_->WriteAt(std::move(data), offset(), &actual);
  if (status == ZX_OK) {
    set_offset(offset() + actual);
  }
  callback(status, actual);
}

void FileConnection::WriteAt(std::vector<uint8_t> data, uint64_t offset, WriteAtCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(ZX_ERR_ACCESS_DENIED, 0);
    return;
  }
  uint64_t actual = 0u;
  zx_status_t status = vn_->WriteAt(std::move(data), offset, &actual);
  callback(status, actual);
}

void FileConnection::Seek(int64_t new_offset, fuchsia::io::SeekOrigin seek, SeekCallback callback) {
  int64_t cur_len = vn_->GetLength();
  size_t capacity = vn_->GetCapacity();
  uint64_t calculated_offset = 0u;
  // TODO: This code does not appear to handle overflow.
  switch (seek) {
    case fuchsia::io::SeekOrigin::START:
      calculated_offset = new_offset;
      break;
    case fuchsia::io::SeekOrigin::CURRENT:
      calculated_offset = offset() + new_offset;
      break;
    case fuchsia::io::SeekOrigin::END:
      calculated_offset = cur_len + new_offset;
      break;
    default:
      callback(ZX_ERR_INVALID_ARGS, 0u);
      return;
  }

  if (static_cast<size_t>(calculated_offset) > capacity) {
    callback(ZX_ERR_OUT_OF_RANGE, offset());
    return;
  }
  set_offset(calculated_offset);
  callback(ZX_OK, offset());
}

void FileConnection::Truncate(uint64_t length, TruncateCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(ZX_ERR_ACCESS_DENIED);
    return;
  }
  callback(vn_->Truncate(length));
}

void FileConnection::GetFlags(GetFlagsCallback callback) { callback(ZX_OK, flags()); }

void FileConnection::SetFlags(uint32_t flags, SetFlagsCallback callback) {
  // TODO: Implement set flags.
  callback(ZX_ERR_NOT_SUPPORTED);
}

void FileConnection::GetBuffer(uint32_t flags, GetBufferCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED, nullptr);
}

void FileConnection::SendOnOpenEvent(zx_status_t status) {
  binding_.events().OnOpen(status, NodeInfoIfStatusOk(vn_, status));
}

}  // namespace internal
}  // namespace vfs

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/internal/file_connection.h>

#include <utility>

#include <lib/vfs/cpp/file.h>

namespace vfs {
namespace internal {

FileConnection::FileConnection(uint32_t flags, vfs::File* vn)
    : Connection(flags), vn_(vn), binding_(this) {}

FileConnection::~FileConnection() = default;

zx_status_t FileConnection::Bind(zx::channel request,
                                 async_dispatcher_t* dispatcher) {
  zx_status_t status = binding_.Bind(std::move(request), dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  binding_.set_error_handler(
      [this](zx_status_t status) { vn_->RemoveConnection(this); });
  return ZX_OK;
}

void FileConnection::Clone(uint32_t flags,
                           fidl::InterfaceRequest<fuchsia::io::Node> object) {
  Connection::Clone(vn_, flags, std::move(object), binding_.dispatcher());
}

void FileConnection::Close(CloseCallback callback) {
  Connection::Close(vn_, std::move(callback));
}

void FileConnection::Describe(DescribeCallback callback) {
  Connection::Describe(vn_, std::move(callback));
}

void FileConnection::Sync(SyncCallback callback) {
  Connection::Sync(vn_, std::move(callback));
}

void FileConnection::GetAttr(GetAttrCallback callback) {
  Connection::GetAttr(vn_, std::move(callback));
}

void FileConnection::SetAttr(uint32_t flags,
                             fuchsia::io::NodeAttributes attributes,
                             SetAttrCallback callback) {
  Connection::SetAttr(vn_, flags, attributes, std::move(callback));
}

void FileConnection::Ioctl(uint32_t opcode, uint64_t max_out,
                           std::vector<zx::handle> handles,
                           std::vector<uint8_t> in, IoctlCallback callback) {
  Connection::Ioctl(vn_, opcode, max_out, std::move(handles), std::move(in),
                    std::move(callback));
}

void FileConnection::Read(uint64_t count, ReadCallback callback) {
  // TODO: Check flags.
  std::vector<uint8_t> data;
  zx_status_t status = vn_->ReadAt(count, offset(), &data);
  if (status == ZX_OK) {
    set_offset(offset() + data.size());
  }
  callback(status, std::vector<uint8_t>(std::move(data)));
}

void FileConnection::ReadAt(uint64_t count, uint64_t offset,
                            ReadAtCallback callback) {
  // TODO: Check flags.
  std::vector<uint8_t> data;
  zx_status_t status = vn_->ReadAt(count, offset, &data);
  callback(status, std::vector<uint8_t>(std::move(data)));
}

void FileConnection::Write(std::vector<uint8_t> data, WriteCallback callback) {
  // TODO: Check flags.
  uint64_t actual = 0u;
  zx_status_t status = vn_->WriteAt(std::move(data), offset(), &actual);
  if (status == ZX_OK) {
    set_offset(offset() + actual);
  }
  callback(status, actual);
}

void FileConnection::WriteAt(std::vector<uint8_t> data, uint64_t offset,
                             WriteAtCallback callback) {
  // TODO: Check flags.
  uint64_t actual = 0u;
  zx_status_t status = vn_->WriteAt(std::move(data), offset, &actual);
  callback(status, actual);
}

void FileConnection::Seek(int64_t offset, fuchsia::io::SeekOrigin start,
                          SeekCallback callback) {
  // TODO: Check flags.
  // TODO: Implement seek.
  callback(ZX_OK, 0u);
}

void FileConnection::Truncate(uint64_t length, TruncateCallback callback) {
  // TODO: Check flags.
  callback(vn_->Truncate(length));
}

void FileConnection::GetFlags(GetFlagsCallback callback) {
  callback(ZX_OK, flags());
}

void FileConnection::SetFlags(uint32_t flags, SetFlagsCallback callback) {
  // TODO: Implement set flags.
  callback(ZX_ERR_NOT_SUPPORTED);
}

void FileConnection::GetBuffer(uint32_t flags, GetBufferCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED, nullptr);
}

}  // namespace internal
}  // namespace vfs

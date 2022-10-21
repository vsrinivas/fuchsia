// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/internal/file_connection.h>
#include <zircon/errors.h>

#include <utility>

namespace vfs {
namespace internal {

FileConnection::FileConnection(fuchsia::io::OpenFlags flags, vfs::internal::File* vn)
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

void FileConnection::AdvisoryLock(fuchsia::io::AdvisoryLockRequest request,
                                  AdvisoryLockCallback callback) {
  callback(fuchsia::io::AdvisoryLocking_AdvisoryLock_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
}

void FileConnection::Clone(fuchsia::io::OpenFlags flags,
                           fidl::InterfaceRequest<fuchsia::io::Node> object) {
  Connection::Clone(vn_, flags, object.TakeChannel(), binding_.dispatcher());
}

void FileConnection::Close(CloseCallback callback) { Connection::Close(vn_, std::move(callback)); }

void FileConnection::Query(QueryCallback callback) {
  const std::string_view kProtocol = fuchsia::io::FILE_PROTOCOL_NAME;
  callback({kProtocol.begin(), kProtocol.end()});
}

void FileConnection::DescribeDeprecated(DescribeDeprecatedCallback callback) {
  Connection::Describe(vn_, std::move(callback));
}

void FileConnection::Describe2(Describe2Callback callback) {
  DescribeDeprecated([callback = std::move(callback)](fuchsia::io::NodeInfoDeprecated node) {
    ZX_ASSERT_MSG(node.is_file(), "FileConnection::Describe returned %lu, expected %lu",
                  node.Which(), fuchsia::io::NodeInfoDeprecated::Tag::kFile);
    fuchsia::io::FileObject& object = node.file();
    fuchsia::io::FileInfo info;
    if (object.event.is_valid()) {
      info.set_observer(std::move(object.event));
    }
    if (object.stream.is_valid()) {
      info.set_stream(std::move(object.stream));
    }
    callback(std::move(info));
  });
}

void FileConnection::GetConnectionInfo(GetConnectionInfoCallback callback) {
  Connection::GetConnectionInfo(vn_, std::move(callback));
}

void FileConnection::Sync(SyncCallback callback) { Connection::Sync(vn_, std::move(callback)); }

void FileConnection::GetAttr(GetAttrCallback callback) {
  Connection::GetAttr(vn_, std::move(callback));
}

void FileConnection::SetAttr(fuchsia::io::NodeAttributeFlags flags,
                             fuchsia::io::NodeAttributes attributes, SetAttrCallback callback) {
  Connection::SetAttr(vn_, flags, attributes, std::move(callback));
}

void FileConnection::Read(uint64_t count, ReadCallback callback) {
  if (!Flags::IsReadable(flags())) {
    callback(fpromise::error(ZX_ERR_BAD_HANDLE));
    return;
  }
  if (count > fuchsia::io::MAX_TRANSFER_SIZE) {
    callback(fpromise::error(ZX_ERR_OUT_OF_RANGE));
    return;
  }
  std::vector<uint8_t> data;
  zx_status_t status = vn_->ReadAt(count, offset(), &data);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  set_offset(offset() + data.size());
  callback(fuchsia::io::Readable_Read_Result::WithResponse(
      fuchsia::io::Readable_Read_Response(std::move(data))));
}

void FileConnection::ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) {
  if (!Flags::IsReadable(flags())) {
    callback(fpromise::error(ZX_ERR_BAD_HANDLE));
    return;
  }
  if (count > fuchsia::io::MAX_TRANSFER_SIZE) {
    callback(fpromise::error(ZX_ERR_OUT_OF_RANGE));
    return;
  }
  std::vector<uint8_t> data;
  zx_status_t status = vn_->ReadAt(count, offset, &data);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  callback(fuchsia::io::File_ReadAt_Result::WithResponse(
      fuchsia::io::File_ReadAt_Response(std::move(data))));
}

void FileConnection::Write(std::vector<uint8_t> data, WriteCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(fpromise::error(ZX_ERR_BAD_HANDLE));
    return;
  }
  uint64_t actual;
  zx_status_t status = vn_->WriteAt(std::move(data), offset(), &actual);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  set_offset(offset() + actual);
  callback(fuchsia::io::Writable_Write_Result::WithResponse(
      fuchsia::io::Writable_Write_Response(actual)));
}

void FileConnection::WriteAt(std::vector<uint8_t> data, uint64_t offset, WriteAtCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(fpromise::error(ZX_ERR_BAD_HANDLE));
    return;
  }
  uint64_t actual;
  zx_status_t status = vn_->WriteAt(std::move(data), offset, &actual);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  callback(
      fuchsia::io::File_WriteAt_Result::WithResponse(fuchsia::io::File_WriteAt_Response(actual)));
}

void FileConnection::Seek(fuchsia::io::SeekOrigin origin, int64_t offset, SeekCallback callback) {
  // TODO: This code does not appear to negative offsets.
  // TODO: This code does not appear to handle overflow.
  uint64_t offset_from_start = offset + ([origin, this]() -> uint64_t {
                                 switch (origin) {
                                   case fuchsia::io::SeekOrigin::START:
                                     return 0;
                                   case fuchsia::io::SeekOrigin::CURRENT:
                                     return this->offset();
                                   case fuchsia::io::SeekOrigin::END:
                                     return vn_->GetLength();
                                 }
                               })();

  if (offset_from_start > vn_->GetCapacity()) {
    callback(fpromise::error(ZX_ERR_OUT_OF_RANGE));
    return;
  }
  set_offset(offset_from_start);
  callback(fuchsia::io::File_Seek_Result::WithResponse(
      fuchsia::io::File_Seek_Response(offset_from_start)));
}

void FileConnection::Resize(uint64_t length, ResizeCallback callback) {
  if (!Flags::IsWritable(flags())) {
    callback(fpromise::error(ZX_ERR_BAD_HANDLE));
    return;
  }
  zx_status_t status = vn_->Truncate(length);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
    return;
  }
  callback(fpromise::ok());
}

void FileConnection::GetBackingMemory(fuchsia::io::VmoFlags vmo_flags,
                                      GetBackingMemoryCallback callback) {
  if ((vmo_flags & fuchsia::io::VmoFlags::READ) != fuchsia::io::VmoFlags{}) {
    if (!Flags::IsReadable(flags())) {
      callback(fpromise::error(ZX_ERR_ACCESS_DENIED));
      return;
    }
  }
  if ((vmo_flags & fuchsia::io::VmoFlags::WRITE) != fuchsia::io::VmoFlags{}) {
    if (!Flags::IsWritable(flags())) {
      callback(fpromise::error(ZX_ERR_ACCESS_DENIED));
      return;
    }
  }
  zx::vmo vmo;
  zx_status_t status = vn_->GetBackingMemory(vmo_flags, &vmo);
  if (status != ZX_OK) {
    callback(fpromise::error(status));
  } else {
    callback(fpromise::ok(std::move(vmo)));
  }
}

void FileConnection::SendOnOpenEvent(zx_status_t status) {
  binding_.events().OnOpen(status, NodeInfoIfStatusOk(vn_, status));
}

void FileConnection::GetFlags(GetFlagsCallback callback) {
  callback(ZX_OK, flags() & (Flags::kStatusFlags | Flags::kFsRights));
}

void FileConnection::SetFlags(fuchsia::io::OpenFlags flags, SetFlagsCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED);
}

void FileConnection::QueryFilesystem(QueryFilesystemCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED, nullptr);
}

}  // namespace internal
}  // namespace vfs

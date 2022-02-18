// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_INTERNAL_FILE_CONNECTION_H_
#define LIB_VFS_CPP_INTERNAL_FILE_CONNECTION_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/vfs/cpp/internal/connection.h>

#include <memory>

namespace vfs {
namespace internal {
class File;

// Binds an implementation of |fuchsia.io.File| to a |vfs::internal::File|.
class FileConnection final : public Connection, public fuchsia::io::File {
 public:
  // Create a connection to |vn| with the given |flags|.
  FileConnection(uint32_t flags, vfs::internal::File* vn);
  ~FileConnection() override;

  // Start listening for |fuchsia.io.File| messages on |request|.
  zx_status_t BindInternal(zx::channel request, async_dispatcher_t* dispatcher) override;

  // |fuchsia::io::File| Implementation:
  void AdvisoryLock(fuchsia::io::AdvisoryLockRequest request,
                    AdvisoryLockCallback callback) override;
  void Clone(uint32_t flags, fidl::InterfaceRequest<fuchsia::io::Node> object) override;
  void CloseDeprecated(CloseDeprecatedCallback callback) override;
  void Close(CloseCallback callback) override;
  void Describe(DescribeCallback callback) override;
  void Describe2(fuchsia::io::ConnectionInfoQuery query, Describe2Callback callback) override;
  void SyncDeprecated(SyncDeprecatedCallback callback) override;
  void Sync(SyncCallback callback) override;
  void GetAttr(GetAttrCallback callback) override;
  void SetAttr(uint32_t flags, fuchsia::io::NodeAttributes attributes,
               SetAttrCallback callback) override;
  void ReadDeprecated(uint64_t count, ReadDeprecatedCallback callback) override;
  void Read(uint64_t count, ReadCallback callback) override;
  void ReadAtDeprecated(uint64_t count, uint64_t offset,
                        ReadAtDeprecatedCallback callback) override;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) override;
  void WriteDeprecated(std::vector<uint8_t> data, WriteDeprecatedCallback callback) override;
  void Write(std::vector<uint8_t> data, WriteCallback callback) override;
  void WriteAtDeprecated(std::vector<uint8_t> data, uint64_t offset,
                         WriteAtDeprecatedCallback callback) override;
  void WriteAt(std::vector<uint8_t> data, uint64_t offset, WriteAtCallback callback) override;
  void SeekDeprecated(int64_t offset, fuchsia::io::SeekOrigin start,
                      SeekDeprecatedCallback callback) override;
  void Seek(fuchsia::io::SeekOrigin start, int64_t offset, SeekCallback callback) override;
  void Truncate(uint64_t length, TruncateCallback callback) override;
  void Resize(uint64_t length, ResizeCallback callback) override;
  void GetFlagsDeprecatedUseNode(GetFlagsDeprecatedUseNodeCallback callback) override;
  void SetFlagsDeprecatedUseNode(uint32_t flags,
                                 SetFlagsDeprecatedUseNodeCallback callback) override;
  void GetBuffer(uint32_t flags, GetBufferCallback callback) override;
  void GetBackingMemory(fuchsia::io::VmoFlags flags, GetBackingMemoryCallback callback) override;
  void GetFlags(GetFlagsCallback callback) override;
  void SetFlags(uint32_t flags, SetFlagsCallback callback) override;
  void QueryFilesystem(QueryFilesystemCallback callback) override;

 protected:
  // |Connection| Implementation:
  void SendOnOpenEvent(zx_status_t status) override;

 private:
  vfs::internal::File* vn_;
  fidl::Binding<fuchsia::io::File> binding_;
};

}  // namespace internal
}  // namespace vfs

#endif  // LIB_VFS_CPP_INTERNAL_FILE_CONNECTION_H_

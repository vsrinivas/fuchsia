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
  FileConnection(fuchsia::io::OpenFlags flags, vfs::internal::File* vn);
  ~FileConnection() override;

  // Start listening for |fuchsia.io.File| messages on |request|.
  zx_status_t BindInternal(zx::channel request, async_dispatcher_t* dispatcher) override;

  // |fuchsia::io::File| Implementation:
  void AdvisoryLock(fuchsia::io::AdvisoryLockRequest request,
                    AdvisoryLockCallback callback) override;
  void Clone(fuchsia::io::OpenFlags flags,
             fidl::InterfaceRequest<fuchsia::io::Node> object) override;
  void Close(CloseCallback callback) override;
  void Query(QueryCallback callback) override;
  void DescribeDeprecated(DescribeDeprecatedCallback callback) override;
  void Describe(DescribeCallback callback) override;
  void GetConnectionInfo(GetConnectionInfoCallback callback) override;
  void Sync(SyncCallback callback) override;
  void GetAttr(GetAttrCallback callback) override;
  void SetAttr(fuchsia::io::NodeAttributeFlags flags, fuchsia::io::NodeAttributes attributes,
               SetAttrCallback callback) override;
  void Read(uint64_t count, ReadCallback callback) override;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCallback callback) override;
  void Write(std::vector<uint8_t> data, WriteCallback callback) override;
  void WriteAt(std::vector<uint8_t> data, uint64_t offset, WriteAtCallback callback) override;
  void Seek(fuchsia::io::SeekOrigin origin, int64_t offset, SeekCallback callback) override;
  void Resize(uint64_t length, ResizeCallback callback) override;
  void GetBackingMemory(fuchsia::io::VmoFlags flags, GetBackingMemoryCallback callback) override;
  void GetFlags(GetFlagsCallback callback) override;
  void SetFlags(fuchsia::io::OpenFlags flags, SetFlagsCallback callback) override;
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

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/llcpp/fidl.h>

#include "src/lib/storage/vfs/cpp/file_connection.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

namespace internal {

class RemoteFileConnection final : public FileConnection {
 public:
  // Refer to documentation for |Connection::Connection|.
  RemoteFileConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                       VnodeConnectionOptions options);

  ~RemoteFileConnection() final = default;

 private:
  //
  // |fuchsia.io/File| operations.
  //

  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) final;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;
  void WriteAt(WriteAtRequestView request, WriteAtCompleter::Sync& completer) final;
  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) final;

  // Current seek offset.
  size_t offset_ = 0;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_STREAM_FILE_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_STREAM_FILE_CONNECTION_H_

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

class StreamFileConnection final : public FileConnection {
 public:
  // Refer to documentation for |Connection::Connection|.
  StreamFileConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::stream stream,
                       VnodeProtocol protocol, VnodeConnectionOptions options);

  ~StreamFileConnection() final = default;

 private:
  //
  // |fuchsia.io/File| operations.
  //

  void Read(uint64_t count, ReadCompleter::Sync& completer) final;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync& completer) final;
  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync& completer) final;
  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync& completer) final;
  void Seek(int64_t offset, fuchsia_io::wire::SeekOrigin start,
            SeekCompleter::Sync& completer) final;

  zx::stream stream_;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_STREAM_FILE_CONNECTION_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fidl/fuchsia.io/cpp/wire.h>

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

  //
  // |fuchsia.io/Node| operations.
  //

  void GetFlags(GetFlagsCompleter::Sync& completer) final;
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) final;

  zx_status_t ReadInternal(void* data, size_t len, size_t* out_actual);
  zx_status_t ReadAtInternal(void* data, size_t len, size_t offset, size_t* out_actual);
  zx_status_t WriteInternal(const void* data, size_t len, size_t* out_actual);
  zx_status_t WriteAtInternal(const void* data, size_t len, size_t offset, size_t* out_actual);
  zx_status_t SeekInternal(fuchsia_io::wire::SeekOrigin origin, int64_t offset);

  // Current seek offset.
  size_t offset_ = 0;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_CONNECTION_H_

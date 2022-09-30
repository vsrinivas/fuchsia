// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_REMOTE_DIR_H_
#define SRC_LIB_STORAGE_VFS_CPP_REMOTE_DIR_H_

#include <fidl/fuchsia.io/cpp/wire.h>

#include <fbl/macros.h>

#include "vnode.h"

namespace fs {

// A remote directory holds a channel to a remotely hosted directory to which requests are delegated
// when opened.
//
// This class is designed to allow programs to publish remote filesystems as directories without
// requiring a separate "mount" step.  In effect, a remote directory is "mounted" at creation time.
//
// It is not possible for the client to detach the remote directory or to mount a new one in its
// place.
//
// This class is thread-safe.
class RemoteDir : public Vnode {
 public:
  // Construct with fbl::MakeRefCounted.

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(VnodeAttributes* a) final;
  fidl::UnownedClientEnd<fuchsia_io::Directory> GetRemote() const final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;

 protected:
  friend fbl::internal::MakeRefCountedHelper<RemoteDir>;
  friend fbl::RefPtr<RemoteDir>;

  // Binds to a remotely hosted directory using the specified FIDL client channel endpoint.  The
  // channel must be valid.
  explicit RemoteDir(fidl::ClientEnd<fuchsia_io::Directory> remote_dir_client);

  // Releases the remotely hosted directory.
  ~RemoteDir() override;

 private:
  fidl::ClientEnd<fuchsia_io::Directory> const remote_dir_client_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RemoteDir);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_REMOTE_DIR_H_

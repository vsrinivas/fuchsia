// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_H_
#define SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_H_

#include <fidl/fuchsia.io/cpp/wire.h>

#include <fbl/macros.h>
#include <fbl/string.h>

#include "vnode.h"

namespace fs {

// A remote file holds a channel to a remotely hosted file to which requests are delegated when
// opened.
//
// This class is designed to allow programs to publish remote files without requiring a separate
// "mount" step.  In effect, a remote file is "mounted" at creation time.
//
// It is not possible for the client to detach the remote file or to mount a new one in its place.
//
// This class is thread-safe.
class RemoteFile : public Vnode {
 public:
  // Construct with fbl::MakeRefCounted.

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(VnodeAttributes* a) final;
  fidl::UnownedClientEnd<fuchsia_io::Directory> GetRemote() const final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;

 private:
  friend fbl::internal::MakeRefCountedHelper<RemoteFile>;
  friend fbl::RefPtr<RemoteFile>;

  // Binds to a remotely hosted file using the specified FIDL client channel endpoint.  The channel
  // must be valid.
  //
  // Note: the endpoint is of type |fuchsia.io/Directory|, because the "file" is still opened using
  // the |fuchsia.io/Directory.Open| method. In a sense, the remote file speaks the combination of
  // file/directory protocols. If we change to using |fuchsia.io/Node.Clone| to open this file, it
  // might make sense to change this endpoint type to |Node| instead.
  explicit RemoteFile(fidl::ClientEnd<fuchsia_io::Directory> remote_client);

  // Releases the remotely hosted file.
  ~RemoteFile() override;

  fidl::ClientEnd<fuchsia_io::Directory> const remote_client_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RemoteFile);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_REMOTE_FILE_H_

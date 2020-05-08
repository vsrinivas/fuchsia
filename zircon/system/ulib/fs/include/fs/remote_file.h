// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_REMOTE_FILE_H_
#define FS_REMOTE_FILE_H_

#include <fbl/macros.h>
#include <fbl/string.h>

#include "vnode.h"

namespace fs {

// A remote file holds a channel to a remotely hosted file to
// which requests are delegated when opened.
//
// This class is designed to allow programs to publish remote files
// without requiring a separate "mount" step.  In effect, a remote file is
// "mounted" at creation time.
//
// It is not possible for the client to detach the remote file or
// to mount a new one in its place.
//
// This class is thread-safe.
class RemoteFile : public Vnode {
 public:
  // Binds to a remotely hosted file using the specified FIDL client
  // channel endpoint.  The channel must be valid.
  explicit RemoteFile(zx::channel remote_client);

  // Releases the remotely hosted file.
  ~RemoteFile() override;

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(VnodeAttributes* a) final;
  bool IsRemote() const final;
  zx_handle_t GetRemote() const final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;

 private:
  zx::channel const remote_client_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RemoteFile);
};

}  // namespace fs

#endif  // FS_REMOTE_FILE_H_

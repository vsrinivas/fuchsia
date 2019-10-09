// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_SERVICE_H_
#define FS_SERVICE_H_

#include <lib/zx/channel.h>

#include <fbl/function.h>
#include <fbl/macros.h>

#include "vnode.h"

namespace fs {

// A node which binds a channel to a service implementation when opened.
//
// This class is thread-safe.
class Service : public Vnode {
 public:
  // Handler called to bind the provided channel to an implementation
  // of the service.
  using Connector = fbl::Function<zx_status_t(zx::channel channel)>;

  // Creates a service with the specified connector.
  //
  // If the |connector| is null, then incoming connection requests will be dropped.
  Service(Connector connector);

  // Destroys the services and releases its connector.
  ~Service() override;

  // |Vnode| implementation:
  zx_status_t ValidateOptions(VnodeConnectionOptions options) final;
  zx_status_t Getattr(vnattr_t* a) final;
  zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, VnodeConnectionOptions options) final;
  bool IsDirectory() const final;
  zx_status_t GetNodeInfo(Rights rights, fuchsia_io_NodeInfo* info) final;

 private:
  Connector connector_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Service);
};

}  // namespace fs

#endif  // FS_SERVICE_H_

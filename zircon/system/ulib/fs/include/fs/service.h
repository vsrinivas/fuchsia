// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_SERVICE_H_
#define FS_SERVICE_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>

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
  using Connector = fit::function<zx_status_t(zx::channel channel)>;

  // Creates a service with the specified connector.
  //
  // If the |connector| is null, then incoming connection requests will be dropped.
  explicit Service(Connector connector);

  // Destroys the services and releases its connector.
  ~Service() override;

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t ConnectService(zx::channel channel) final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;

 private:
  Connector connector_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Service);
};

}  // namespace fs

#endif  // FS_SERVICE_H_

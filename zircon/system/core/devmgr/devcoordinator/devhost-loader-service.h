// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DEVHOST_LOADER_SERVICE_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DEVHOST_LOADER_SERVICE_H_

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <loader-service/loader-service.h>

namespace devmgr {

// A loader service for devhosts that restricts access to dynamic libraries.
class DevhostLoaderService {
 public:
  // Create a new loader service for devhosts. The |dispatcher| must have a
  // longer lifetime than |out|.
  static zx_status_t Create(async_dispatcher_t* dispatcher,
                            fbl::unique_ptr<DevhostLoaderService>* out);
  ~DevhostLoaderService();

  // Connect to the loader service.
  zx_status_t Connect(zx::channel* out);

  // Return the file descriptor for the root namespace of the loader service.
  const fbl::unique_fd& root() const { return root_; }

 private:
  fbl::unique_fd root_;
  loader_service_t* svc_ = nullptr;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_DEVHOST_LOADER_SERVICE_H_

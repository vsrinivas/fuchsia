// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_BOOTSVC_BOOTFS_LOADER_SERVICE_H_
#define SRC_BRINGUP_BIN_BOOTSVC_BOOTFS_LOADER_SERVICE_H_

#include <lib/async/dispatcher.h>

#include <fbl/ref_ptr.h>

#include "bootfs-service.h"
#include "src/lib/loader_service/loader_service.h"

namespace bootsvc {

class BootfsLoaderService : public loader::LoaderServiceBase {
 public:
  // Create a loader that loads from the given bootfs service and dispatches
  // on the given dispatcher.
  static std::shared_ptr<BootfsLoaderService> Create(async_dispatcher_t* dispatcher,
                                                     fbl::RefPtr<BootfsService> bootfs);
  static std::shared_ptr<BootfsLoaderService> Create(async_dispatcher_t* dispatcher,
                                                     const zx::vmo& bootfs,
                                                     const zx::vmo& bootfs_exec);

 private:
  BootfsLoaderService(async_dispatcher_t* dispatcher, fbl::RefPtr<BootfsService> bootfs,
                      zx::vmo vmo, zx::vmo vmo_exec)
      : LoaderServiceBase(dispatcher, "bootsvc"),
        bootfs_(std::move(bootfs)),
        bootfs_vmo_(std::move(vmo)),
        bootfs_vmo_exec_(std::move(vmo_exec)) {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;

  // The bootfs loader service can find the required libraries via either the
  // C++ bootfs VFS (if it exists), or directly from the bootfs VMO if the
  // component manager's Rust bootfs VFS is going to be used instead.
  //
  // This is a temporary state. Bootsvc is in the process of being deprecated,
  // and userboot will load the libraries required for component manager to start
  // instead.
  fbl::RefPtr<BootfsService> bootfs_;

  zx::vmo bootfs_vmo_;
  zx::vmo bootfs_vmo_exec_;
};

}  // namespace bootsvc

#endif  // SRC_BRINGUP_BIN_BOOTSVC_BOOTFS_LOADER_SERVICE_H_

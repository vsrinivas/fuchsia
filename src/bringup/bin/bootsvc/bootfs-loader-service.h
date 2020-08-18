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

 private:
  BootfsLoaderService(async_dispatcher_t* dispatcher, fbl::RefPtr<BootfsService> bootfs)
      : LoaderServiceBase(dispatcher, "bootsvc"), bootfs_(std::move(bootfs)) {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;

  fbl::RefPtr<BootfsService> bootfs_;
};

}  // namespace bootsvc

#endif  // SRC_BRINGUP_BIN_BOOTSVC_BOOTFS_LOADER_SERVICE_H_

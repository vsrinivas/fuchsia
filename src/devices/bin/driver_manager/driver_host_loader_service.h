// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_LOADER_SERVICE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_LOADER_SERVICE_H_

#include "src/lib/loader_service/loader_service.h"

// A loader service for driver_hosts that restricts access to dynamic libraries by applying an
// allowlist, but then otherwise simply loads them from the given lib directory.
class DriverHostLoaderService : public loader::LoaderService {
 public:
  static std::shared_ptr<DriverHostLoaderService> Create(async_dispatcher_t* dispatcher,
                                                         fbl::unique_fd lib_fd,
                                                         std::string name = "driver_host");

 private:
  DriverHostLoaderService(async_dispatcher_t* dispatcher, fbl::unique_fd lib_fd, std::string name)
      : LoaderService(dispatcher, std::move(lib_fd), std::move(name)) {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DRIVER_HOST_LOADER_SERVICE_H_

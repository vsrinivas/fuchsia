// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_DEPRECATED_LOADER_SERVICE_H_
#define SRC_STORAGE_FSHOST_DEPRECATED_LOADER_SERVICE_H_

#include "src/lib/loader_service/loader_service.h"

// Concrete implementation of a fuchsia.ldsvc.Loader FIDL server that serves libraries from
// "system/lib/" and "boot/lib/" subdirectories within the given directory, in that order of
// precedence. (If the library is not present or fails to load from "system/lib/" for any reason,
// "boot/lib/" will be tried.)
//
// TODO(fxbug.dev/34633): This loader service implementation is DEPRECATED and should be deleted.
class DeprecatedBootSystemLoaderService : public loader::LoaderService {
 public:
  // This takes ownership of the `root_dir` fd and will close it automatically once all connections
  // to the loader service are closed and copies of this object are destroyed. `name` is used to
  // provide context when logging.
  //
  // The given `root_dir` should contain "system/lib/" and "boot/lib/" subdirectories which in turn
  // contain the libraries to be loaded.
  //
  // These directories do not need to be present at loader service creation time, and their contents
  // can change. For example, this is typically used in scenarios where "system/lib/" has not yet
  // been mounted and is either empty or does not exist, and is mounted after the loader service is
  // created and has active connections. This works as expected; newly available libraries will
  // start being returned once present. This also means that the VMO contents returned from the
  // loader service may change over time.
  static std::shared_ptr<DeprecatedBootSystemLoaderService> Create(async_dispatcher_t* dispatcher,
                                                                   fbl::unique_fd lib_dir,
                                                                   std::string name);

 private:
  DeprecatedBootSystemLoaderService(async_dispatcher_t* dispatcher, fbl::unique_fd root_dir,
                                    std::string name)
      : LoaderService(dispatcher, std::move(root_dir), std::move(name)) {}

  virtual zx::status<zx::vmo> LoadObjectImpl(std::string path) override;
};

#endif  // SRC_STORAGE_FSHOST_DEPRECATED_LOADER_SERVICE_H_

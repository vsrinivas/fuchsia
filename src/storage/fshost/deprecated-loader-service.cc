// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/deprecated-loader-service.h"

#include "src/lib/files/path.h"

// static
std::shared_ptr<DeprecatedBootSystemLoaderService> DeprecatedBootSystemLoaderService::Create(
    async_dispatcher_t* dispatcher, fbl::unique_fd lib_dir, std::string name) {
  // Can't use make_shared because constructor is private
  return std::shared_ptr<DeprecatedBootSystemLoaderService>(
      new DeprecatedBootSystemLoaderService(dispatcher, std::move(lib_dir), std::move(name)));
}

zx::status<zx::vmo> DeprecatedBootSystemLoaderService::LoadObjectImpl(std::string path) {
  std::string system_path = files::JoinPath("system/lib", path);
  auto status = LoaderService::LoadObjectImpl(system_path);
  if (status.is_ok()) {
    return status.take_value();
  }

  std::string boot_path = files::JoinPath("boot/lib", path);
  return LoaderService::LoadObjectImpl(boot_path);
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/bootsvc/bootfs-loader-service.h"

#include <zircon/boot/bootfs.h>

#include "src/lib/files/path.h"

namespace bootsvc {

// static
std::shared_ptr<BootfsLoaderService> BootfsLoaderService::Create(
    async_dispatcher_t* dispatcher, fbl::RefPtr<BootfsService> bootfs) {
  // Can't use make_shared because constructor is private
  return std::shared_ptr<BootfsLoaderService>(
      new BootfsLoaderService(dispatcher, std::move(bootfs)));
}

zx::status<zx::vmo> BootfsLoaderService::LoadObjectImpl(std::string path) {
  std::string lib_path = files::JoinPath("lib", path);

  uint64_t size;
  zx::vmo vmo;
  zx_status_t status = bootfs_->Open(lib_path.c_str(), /*executable=*/true, &vmo, &size);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(vmo));
}

}  // namespace bootsvc

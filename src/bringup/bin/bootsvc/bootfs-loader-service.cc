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
  printf("bootsvc: Creating loader service backed by bootfs VFS...\n");
  // Invalid VMOs. The loader service will use the legacy codepath and query the bootfs service.
  zx::vmo invalid_vmo1, invalid_vmo2;
  return std::shared_ptr<BootfsLoaderService>(new BootfsLoaderService(
      dispatcher, std::move(bootfs), std::move(invalid_vmo1), std::move(invalid_vmo2)));
}

// static
std::shared_ptr<BootfsLoaderService> BootfsLoaderService::Create(async_dispatcher_t* dispatcher,
                                                                 const zx::vmo& bootfs,
                                                                 const zx::vmo& bootfs_exec) {
  printf("bootsvc: Creating loader service backed by bootfs image...\n");
  zx::vmo bootfs_dup, bootfs_exec_dup;
  zx_status_t status = bootfs.duplicate(ZX_RIGHT_SAME_RIGHTS, &bootfs_dup);
  if (status != ZX_OK || !bootfs_dup.is_valid()) {
    printf("bootsvc: failed to duplicate bootfs vmo with error %s\n", zx_status_get_string(status));
    return nullptr;
  }

  status = bootfs_exec.duplicate(ZX_RIGHT_SAME_RIGHTS, &bootfs_exec_dup);
  if (status != ZX_OK || !bootfs_exec_dup.is_valid()) {
    printf("bootsvc: failed to duplicate bootfs_exec vmo with error %s\n",
           zx_status_get_string(status));
    return nullptr;
  }

  return std::shared_ptr<BootfsLoaderService>(new BootfsLoaderService(
      dispatcher, nullptr, std::move(bootfs_dup), std::move(bootfs_exec_dup)));
}

zx::status<zx::vmo> BootfsLoaderService::LoadObjectImpl(std::string path) {
  ZX_ASSERT_MSG(
      !!bootfs_ != (bootfs_vmo_.is_valid() && bootfs_vmo_exec_.is_valid()),
      "BootfsLoaderService either needs a bootfs VMO handle, or an initialized bootfs service.");

  std::string lib_path = files::JoinPath("lib", path);

  if (bootfs_) {
    uint64_t size;
    zx::vmo vmo;
    zx_status_t status = bootfs_->Open(lib_path.c_str(), /*executable=*/true, &vmo, &size);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(vmo));
  } else {
    zbitl::MapUnownedVmo unowned_bootfs(bootfs_vmo_.borrow()),
        unowned_bootfs_exec(bootfs_vmo_exec_.borrow());
    return BootfsService::GetFileFromBootfsVmo(unowned_bootfs, unowned_bootfs_exec, lib_path,
                                               /*size=*/nullptr);
  }
}

}  // namespace bootsvc

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <cstddef>
#include <string_view>

#include <fbl/unique_fd.h>
#include <src/bringup/lib/mexec/mexec.h>

constexpr const char* kMexecZbi = "testdata/mexec-child.zbi";

namespace {

using BootfsView = zbitl::BootfsView<zbitl::MapUnownedVmo>;

// The bootfs VFS (rooted under '/boot') is hosted by component manager. These tests can be started
// directly from userboot without starting component manager, so the bootfs VFS will not be
// available. Instead, we can just read any files needed directly from the uncompressed bootfs VMO.
zx_status_t GetFileFromBootfs(std::string_view path, zbitl::MapUnownedVmo bootfs, zx::vmo* vmo) {
  BootfsView view;
  if (auto result = BootfsView::Create(std::move(bootfs)); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return ZX_ERR_INTERNAL;
  } else {
    view = std::move(result).value();
  }

  auto file = view.find(path);
  if (auto result = view.take_error(); result.is_error()) {
    zbitl::PrintBootfsError(result.error_value());
    return ZX_ERR_INTERNAL;
  }
  if (file == view.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  return view.storage().vmo().create_child(ZX_VMO_CHILD_SNAPSHOT | ZX_VMO_CHILD_NO_WRITE,
                                           file->offset, file->size, vmo);
}

}  // namespace

int main() {
  zx::vmo bootfs(zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0)));
  if (!bootfs.is_valid()) {
    printf("zbi_mexec-test-entry: received an invalid bootfs vmo handle\n");
    return ZX_ERR_INTERNAL;
  }

  zx::vmo kernel_zbi, data_zbi;
  {
    zx::vmo vmo;
    zbitl::MapUnownedVmo unowned_bootfs(bootfs.borrow());
    if (zx_status_t status = GetFileFromBootfs(kMexecZbi, unowned_bootfs, &vmo); status != ZX_OK) {
      printf("zbi-mexec-test-entry: failed to get child ZBI's VMO: %s\n",
             zx_status_get_string(status));
      return status;
    }

    zbitl::View view(std::move(vmo));
    if (view.begin() == view.end()) {
      if (auto result = view.take_error(); result.is_error()) {
        printf("zbi-mexec-test-entry: invalid child ZBI: ");
        zbitl::PrintViewError(result.error_value());
      } else {
        printf("zbi-mexec-test-entry: empty child ZBI\n");
      }
      return ZX_ERR_INTERNAL;
    }

    auto first = view.begin();
    auto second = std::next(first);
    if (auto result = view.Copy(first, second); result.is_error()) {
      printf("zbi-mexec-test-entry: failed to copy out kernel payload: ");
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      kernel_zbi = std::move(result).value();
    }

    if (auto result = view.Copy(second, view.end()); result.is_error()) {
      printf("zbi-mexec-test-entry: failed to copy out data ZBI: ");
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      data_zbi = std::move(result).value();
    }

    if (auto result = view.take_error(); result.is_error()) {
      printf("zbi-mexec-test-entry: ZBI iteration failure: ");
      zbitl::PrintViewError(result.error_value());
      return ZX_ERR_INTERNAL;
    }
  }

  zx::resource resource{zx_take_startup_handle(PA_HND(PA_RESOURCE, 0))};
  if (!resource.is_valid()) {
    printf("zbi-mexec-test-entry: unable to get a hold of the root resource\n");
    return ZX_ERR_INTERNAL;
  }

  if (zx_status_t status = mexec::PrepareDataZbi(resource.borrow(), data_zbi.borrow());
      status != ZX_OK) {
    printf("zbi-mexec-test-entry: failed to prepare data ZBI: %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }

  if (zx_status_t status =
          zx_system_mexec(resource.release(), kernel_zbi.release(), data_zbi.release());
      status != ZX_OK) {
    printf("zbi-mexec-test-entry: zx_system_mexec(): %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/image.h>
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

constexpr const char* kMexecZbi = "/boot/testdata/mexec-child.zbi";

int main() {
  fbl::unique_fd fd{open(kMexecZbi, O_RDONLY)};
  if (!fd) {
    printf("failed to open %s: %s\n", kMexecZbi, strerror(errno));
    return ZX_ERR_INTERNAL;
  }

  zx::vmo kernel_zbi, data_zbi;
  {
    zx::vmo vmo;
    if (zx_status_t status = fdio_get_vmo_exact(fd.get(), vmo.reset_and_get_address());
        status != ZX_OK) {
      printf("failed get child ZBI's VMO: %s\n", zx_status_get_string(status));
      return status;
    }
    zbitl::View view(std::move(vmo));

    if (auto result = view.Copy(view.begin(), ++view.begin()); result.is_error()) {
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      kernel_zbi = std::move(result).value();
    }

    if (auto result = view.Copy(++view.begin(), view.end()); result.is_error()) {
      zbitl::PrintViewCopyError(result.error_value());
      view.ignore_error();
      return ZX_ERR_INTERNAL;
    } else {
      data_zbi = std::move(result).value();
    }

    if (auto result = view.take_error(); result.is_error()) {
      zbitl::PrintViewError(result.error_value());
      return ZX_ERR_INTERNAL;
    }
  }

  zx::resource resource{zx_take_startup_handle(PA_HND(PA_RESOURCE, 0))};
  if (!resource.is_valid()) {
    printf("unable to get a hold of the root resource\n");
    return ZX_ERR_INTERNAL;
  }

  if (zx_status_t status = mexec::PrepareDataZbi(resource.borrow(), data_zbi.borrow());
      status != ZX_OK) {
    return status;
  }

  if (zx_status_t status =
          zx_system_mexec(resource.release(), kernel_zbi.release(), data_zbi.release());
      status != ZX_OK) {
    printf("zx_system_mexec(): %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zbi.h"

#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/vmo.h>
#include <stdio.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

zx_status_t netboot_prepare_zbi(zx::vmo nbkernel, zx::vmo nbdata, std::string_view cmdline,
                                zx::vmo* kernel_zbi, zx::vmo* data_zbi) {
  if (!nbkernel.is_valid()) {
    printf("netbootloader: no kernel!\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (!nbdata.is_valid()) {
    zbitl::View nbkernel_view(std::move(nbkernel));

    // In this case, the netboot "kernel zbi" necessarily must be complete.
    if (auto result = zbitl::CheckComplete(nbkernel_view); result.is_error()) {
      std::string_view error = result.error_value();
      printf("netbootloader: ZBI is not complete : %.*s", static_cast<int>(error.size()),
             error.data());
      return ZX_ERR_INTERNAL;
    }

    auto second = nbkernel_view.begin();
    auto first = second++;

    // Copy the kernel (the first item) into its own ZBI backed by a new VMO
    // (created automatically by zbitl::View).
    if (auto result = nbkernel_view.Copy(first, second); result.is_error()) {
      printf("netbootloader: failed to copy kernel item: ");
      PrintViewCopyError(result.error_value());
      return ZX_ERR_INTERNAL;
    } else {
      *kernel_zbi = std::move(result).value();
    }
    // Ditto for data items (i.e., the remainder).
    if (auto result = nbkernel_view.Copy(second, nbkernel_view.end()); result.is_error()) {
      printf("netbootloader: failed to copy data_zbi items: ");
      PrintViewCopyError(result.error_value());
      return ZX_ERR_INTERNAL;
    } else {
      *data_zbi = std::move(result).value();
    }

    if (auto result = nbkernel_view.take_error(); result.is_error()) {
      printf("netbootloader: failure encountered in iteration over ZBI: ");
      PrintViewError(result.error_value());
      return ZX_ERR_INTERNAL;
    }
  } else {
    // Old-style boot with separate kernel and data ZBIs.
    // TODO(fxbug.dev/63103): delete me.
    printf(
        "netbootloader: old-style boot is deprecated;"
        " switch to complete ZBI!\n");
    *kernel_zbi = std::move(nbkernel);
    *data_zbi = std::move(nbdata);
  }

  zbitl::Image data_img(zx::unowned_vmo{*data_zbi});
  if (!cmdline.empty()) {
    auto append_result = data_img.Append(zbi_header_t{.type = ZBI_TYPE_CMDLINE},
                                         zbitl::AsBytes(cmdline.data(), cmdline.size()));
    if (append_result.is_error()) {
      printf("netbootloader: failed to append command line: ");
      PrintViewError(append_result.error_value());
      return ZX_ERR_INTERNAL;
    }
  }

  zbitl::View kernel_view(zx::unowned_vmo{*kernel_zbi});
  printf("netbootloader: kernel ZBI %#zx bytes; data ZBI %#zx bytes\n", kernel_view.size_bytes(),
         data_img.size_bytes());
  return ZX_OK;
}

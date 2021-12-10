// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/zbi.h"

#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/vmo.h>
#include <stdio.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

zx_status_t netboot_prepare_zbi(zx::vmo zbi_in, std::string_view cmdline, zx::vmo* kernel_zbi,
                                zx::vmo* data_zbi) {
  if (!zbi_in.is_valid()) {
    printf("netbootloader: no kernel!\n");
    return ZX_ERR_INVALID_ARGS;
  }

  zbitl::View view(std::move(zbi_in));

  if (auto result = zbitl::CheckBootable(view); result.is_error()) {
    std::string_view error = result.error_value();
    printf("netbootloader: ZBI is not bootable : %.*s", static_cast<int>(error.size()),
           error.data());
    view.ignore_error();
    return ZX_ERR_INTERNAL;
  }

  auto first = view.begin();
  auto second = std::next(first);
  if (auto result = view.take_error(); result.is_error()) {
    printf("netbootloader: failure encountered in iteration over ZBI: ");
    PrintViewError(result.error_value());
    return ZX_ERR_INTERNAL;
  }

  // Copy the kernel (the first item) into its own ZBI backed by a new VMO
  // (created automatically by zbitl::View).
  if (auto result = view.Copy(first, second); result.is_error()) {
    printf("netbootloader: failed to copy kernel item: ");
    PrintViewCopyError(result.error_value());
    return ZX_ERR_INTERNAL;
  } else {
    *kernel_zbi = std::move(result).value();
  }
  // Ditto for data items (i.e., the remainder).
  if (auto result = view.Copy(second, view.end()); result.is_error()) {
    printf("netbootloader: failed to copy data_zbi items: ");
    PrintViewCopyError(result.error_value());
    return ZX_ERR_INTERNAL;
  } else {
    *data_zbi = std::move(result).value();
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

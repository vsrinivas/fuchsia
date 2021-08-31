// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mexec.h"

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <lib/zircon-internal/align.h>
#include <zircon/status.h>

#include <array>
#include <cstddef>

#include <explicit-memory/bytes.h>
#include <fbl/array.h>

namespace mexec {

namespace {

zx_status_t GetMexecDataZbi(zx::unowned_resource resource, fbl::Array<std::byte>& buff) {
  buff = fbl::Array<std::byte>(new std::byte[zx_system_get_page_size()], zx_system_get_page_size());
  zx_status_t status = zx_system_mexec_payload_get(resource->get(), buff.get(), buff.size());
  // For as long as the buffer is too small, increase it by a page. The maximum
  // allowed size is 16Kib, so this loop is tightly bounded.
  while (status == ZX_ERR_BUFFER_TOO_SMALL) {
    const size_t new_size = buff.size() + zx_system_get_page_size();
    buff = fbl::Array<std::byte>(new std::byte[new_size], new_size);
    status = zx_system_mexec_payload_get(resource->get(), buff.get(), buff.size());
  }
  return status;
}

}  // namespace

zx_status_t Boot(zx::resource resource,
                 fidl::ClientEnd<fuchsia_device_manager::Administrator> devmgr, zx::vmo kernel_zbi,
                 zx::vmo data_zbi) {
  fbl::Array<std::byte> mexec_data_zbi;
  if (auto status = GetMexecDataZbi(resource.borrow(), mexec_data_zbi); status != ZX_OK) {
    printf("mexec::Boot: failed to get the mexec data ZBI: %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  zbitl::View mexec_data_view(zbitl::AsBytes(mexec_data_zbi.data(), mexec_data_zbi.size()));

  zbitl::Image data_image(std::move(data_zbi));
  if (auto result = data_image.Extend(mexec_data_view.begin(), mexec_data_view.end());
      result.is_error()) {
    zbitl::PrintViewCopyError(result.error_value());
    mexec_data_view.ignore_error();
    return ZX_ERR_INTERNAL;
  }
  if (auto result = mexec_data_view.take_error(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return ZX_ERR_INTERNAL;
  }

  std::array<uint8_t, 64> entropy;
  zx_cprng_draw(entropy.data(), entropy.size());
  if (auto result = data_image.Append(zbi_header_t{.type = ZBI_TYPE_SECURE_ENTROPY},
                                      zbitl::AsBytes(entropy.data(), entropy.size()));
      result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return ZX_ERR_INTERNAL;
  }
  mandatory_memset(entropy.data(), 0, entropy.size());
  {
    fidl::WireSyncClient client = fidl::BindSyncClient(std::move(devmgr));
    if (zx_status_t status =
            client.Suspend(fuchsia_device_manager::wire::kSuspendFlagMexec).status();
        status != ZX_OK) {
      printf("mexec::Boot: failed to suspend devices: %s\n", zx_status_get_string(status));
      return ZX_ERR_INTERNAL;
    }
  }

  if (zx_status_t status =
          zx_system_mexec(resource.release(), kernel_zbi.release(), data_image.storage().release());
      status != ZX_OK) {
    printf("mexec::Boot: failed to mexec: %s\n", zx_status_get_string(status));
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

}  // namespace mexec

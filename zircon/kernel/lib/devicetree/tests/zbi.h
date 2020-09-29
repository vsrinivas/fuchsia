// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fuchsia/boot/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>

struct DevicetreeItem {
  zx::vmo vmo;
  uint32_t size;

  static zx::status<DevicetreeItem> Get() {
    fuchsia::boot::ItemsSyncPtr items;
    if (zx_status_t status = fdio_service_connect("/svc/fuchsia.boot.Items",
                                                  items.NewRequest().TakeChannel().release());
        status != ZX_OK) {
      return zx::error{status};
    }

    DevicetreeItem item;
    if (zx_status_t status = (items->Get(ZBI_TYPE_DEVICETREE, 0u, &item.vmo, &item.size));
        status != ZX_OK) {
      return zx::error{status};
    }

    return zx::ok(std::move(item));
  }
};

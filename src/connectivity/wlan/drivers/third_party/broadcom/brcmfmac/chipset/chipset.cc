// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset.h"

#include <zircon/errors.h>
#include <zircon/status.h>

#include <optional>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/cr4_chipset.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {

Chipset::~Chipset() = default;

// static
zx_status_t Chipset::Create(RegisterWindowProviderInterface* register_window_provider,
                            Backplane* backplane, std::unique_ptr<Chipset>* out_chipset) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<Chipset> chipset;
  if (backplane->GetCore(Backplane::CoreId::kArmCr4Core) != nullptr) {
    std::optional<Cr4Chipset> cr4_chipset;
    if ((status = Cr4Chipset::Create(register_window_provider, backplane, &cr4_chipset)) != ZX_OK) {
      BRCMF_ERR("Failed to create ARM CR4 chipset: %s", zx_status_get_string(status));
      return status;
    }
    chipset = std::make_unique<Cr4Chipset>(std::move(cr4_chipset).value());
  }

  *out_chipset = std::move(chipset);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan

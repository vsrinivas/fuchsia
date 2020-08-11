// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset.h"

#include <zircon/errors.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"

namespace wlan {
namespace brcmfmac {

Chipset::~Chipset() = default;

// static
zx_status_t Chipset::Create(RegisterWindowProviderInterface* register_window_provider,
                            Backplane* backplane, std::unique_ptr<Chipset>* out_chipset) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace brcmfmac
}  // namespace wlan

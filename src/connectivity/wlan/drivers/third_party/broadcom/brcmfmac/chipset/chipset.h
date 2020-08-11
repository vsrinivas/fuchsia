// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_H_

#include <zircon/types.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"

namespace wlan {
namespace brcmfmac {

class Backplane;

// This interface represents a brcmfmac chipset.
class Chipset {
 public:
  virtual ~Chipset();

  // Static factory function for Chipset instances.
  static zx_status_t Create(RegisterWindowProviderInterface* register_window_provider,
                            Backplane* backplane, std::unique_ptr<Chipset>* out_chipset);

  // Get the RAM base and size of this chipset.
  virtual uint32_t GetRambase() const = 0;
  virtual size_t GetRamsize() const = 0;

  // Enter and exit the firmware upload state on this ARM core.
  virtual zx_status_t EnterUploadState() = 0;
  virtual zx_status_t ExitUploadState() = 0;

  // Reset this chipset.
  virtual zx_status_t Reset() = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CHIPSET_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CR4_CHIPSET_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CR4_CHIPSET_H_

#include <zircon/types.h>

#include <optional>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset.h"

namespace wlan {
namespace brcmfmac {

// This class implements the ARM CR4 version of the Chipset interface.
class Cr4Chipset : public Chipset {
 public:
  Cr4Chipset();
  Cr4Chipset(Cr4Chipset&& other);
  Cr4Chipset& operator=(Cr4Chipset other);
  friend void swap(Cr4Chipset& lhs, Cr4Chipset& rhs);
  ~Cr4Chipset() override;

  // Static factory function for Cr4Chipset instances.
  static zx_status_t Create(RegisterWindowProviderInterface* register_window_provider,
                            Backplane* backplane, std::optional<Cr4Chipset>* out_chipset);

  // Chipset implementation.
  uint32_t GetRambase() const override;
  size_t GetRamsize() const override;
  zx_status_t EnterUploadState() override;
  zx_status_t ExitUploadState() override;
  zx_status_t Reset() override;

 private:
  RegisterWindowProviderInterface* register_window_provider_ = nullptr;
  Backplane* backplane_ = nullptr;
  size_t ramsize_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_CR4_CHIPSET_H_

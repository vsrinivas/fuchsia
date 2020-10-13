// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_AXI_BACKPLANE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_AXI_BACKPLANE_H_

#include <zircon/types.h>

#include <memory>
#include <optional>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"

namespace wlan {
namespace brcmfmac {

// This implements the Backplane interface for the Advanced Extensible Interface (AXI).
class AxiBackplane : public Backplane {
 public:
  explicit AxiBackplane(CommonCoreId chip_id, uint16_t chip_rev);
  AxiBackplane(AxiBackplane&& other);
  AxiBackplane& operator=(AxiBackplane other);
  friend void swap(AxiBackplane& lhs, AxiBackplane& rhs);
  ~AxiBackplane() override;

  static zx_status_t Create(RegisterWindowProviderInterface* register_window_provider,
                            CommonCoreId chip_id, uint16_t chip_rev,
                            std::optional<AxiBackplane>* out_backplane);

  // Backplane implementation.
  const Backplane::Core* GetCore(Backplane::CoreId core_id) const override;
  zx_status_t IsCoreUp(Backplane::CoreId core_id, bool* out_is_up) override;
  zx_status_t DisableCore(Backplane::CoreId core_id, uint32_t prereset,
                          uint32_t postreset) override;
  zx_status_t ResetCore(Backplane::CoreId core_id, uint32_t prereset, uint32_t postreset) override;

 private:
  struct Core {
    Backplane::Core core = {};
    uint32_t wrapbase = 0;
    size_t wrapsize = 0;
  };

  // Enumerate the cores on the backplane.
  static zx_status_t EnumerateCores(RegisterWindowProviderInterface* register_window_provider,
                                    std::vector<Core>* out_cores);

  // Get the AxiBackplane::Core instance for a particular core id.
  const Core* GetAxiCore(Backplane::CoreId core_id) const;

  // Get the wrapbase register window of a particular core.
  zx_status_t GetWrapWindow(
      Backplane::CoreId core_id,
      std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>* out_wrap_window);

  RegisterWindowProviderInterface* register_window_provider_ = nullptr;
  std::vector<Core> cores_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_AXI_BACKPLANE_H_

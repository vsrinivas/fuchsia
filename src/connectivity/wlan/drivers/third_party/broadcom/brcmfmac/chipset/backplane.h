// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_BACKPLANE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_BACKPLANE_H_

#include <zircon/types.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"

namespace wlan {
namespace brcmfmac {

// This class provides a view of the brcmfmac chipset backplane, which is accessible over the system
// expansion bus.  The backplane provides access to the individual cores of the chipset.
class Backplane {
 public:
  // Enumeration of the core IDs known to the backplane interface.
  enum class CoreId : uint16_t {
    kInvalid = 0,
    kArmCm3Core = 0x82a,
    kInternalMemCore = 0x80e,
    kArmCr4Core = 0x83e,
    kArmCa7Core = 0x847,
    k80211Core = 0x812,
    kPcie2Core = 0x83c,
    kSdioDevCore = 0x829,
    kChipCommonCore = 0x800,
    kSysMemCore = 0x849,
    kPmuCore = 0x827,
    kSimDevCore = 0x4a43,
  };

  // Core properties queryable through GetCore().
  struct Core {
    CoreId id = CoreId::kInvalid;
    uint16_t rev = 0;
    uint32_t regbase = 0;
    size_t regsize = 0;
  };

  Backplane();
  Backplane(const Backplane& other);
  Backplane(Backplane&& other);
  friend void swap(Backplane& lhs, Backplane& rhs);
  virtual ~Backplane();

  static zx_status_t Create(RegisterWindowProviderInterface* register_window_provider,
                            std::unique_ptr<Backplane>* out_backplane);

  // State accessors.
  CommonCoreId chip_id() const;
  uint16_t chip_rev() const;

  // Query the backplane for properties of a core.  Returns `nullptr` iff the core does not exist on
  // the backplane.
  virtual const Core* GetCore(CoreId core_id) const = 0;

  // Query the running state of a core.
  virtual zx_status_t IsCoreUp(CoreId core_id, bool* out_is_up) = 0;

  // Disable or reset a core.
  virtual zx_status_t DisableCore(CoreId core_id, uint32_t prereset, uint32_t postreset) = 0;
  virtual zx_status_t ResetCore(CoreId core_id, uint32_t prereset, uint32_t postreset) = 0;

 protected:
  explicit Backplane(CommonCoreId chip_id, uint16_t chip_rev);

 private:
  CommonCoreId chip_id_ = CommonCoreId::kInvalid;
  uint16_t chip_rev_ = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_BACKPLANE_H_

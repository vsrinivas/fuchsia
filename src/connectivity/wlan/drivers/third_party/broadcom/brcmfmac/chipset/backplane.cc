// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"

#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/bitfield.h"

namespace wlan {
namespace brcmfmac {
namespace {

class ChipIdRegister : public wlan::common::AddressableBitField<uint16_t, uint32_t,
                                                                offsetof(ChipsetCoreRegs, chipid)> {
 public:
  enum class Type : uint32_t {};

  WLAN_BIT_FIELD(id, 0, 16);
  WLAN_BIT_FIELD(rev, 16, 4);
  WLAN_BIT_FIELD(pkg, 20, 4);
  WLAN_BIT_FIELD(cc, 24, 4);
  WLAN_BIT_FIELD(type, 28, 4);
};

bool IsSupportedCore(CommonCoreId chip_id) {
  // This is the list of known supported chip IDs.  Add a chip here once it has been confirmed to
  // work with this driver.
  constexpr CommonCoreId kSupportedChipIds[] = {
      CommonCoreId::kInvalid,
  };

  return std::binary_search(std::begin(kSupportedChipIds), std::end(kSupportedChipIds), chip_id,
                            [](CommonCoreId lhs, CommonCoreId rhs) {
                              return static_cast<uint16_t>(lhs) < static_cast<uint16_t>(rhs);
                            });
};

}  // namespace

Backplane::Backplane() = default;

Backplane::Backplane(CommonCoreId chip_id, uint16_t chip_rev)
    : chip_id_(chip_id), chip_rev_(chip_rev) {}

Backplane::Backplane(const Backplane& other) {
  chip_id_ = other.chip_id_;
  chip_rev_ = other.chip_rev_;
}

Backplane::Backplane(Backplane&& other) { swap(*this, other); }

void swap(Backplane& lhs, Backplane& rhs) {
  using std::swap;
  swap(lhs.chip_id_, rhs.chip_id_);
  swap(lhs.chip_rev_, rhs.chip_rev_);
}

Backplane::~Backplane() = default;

// static
zx_status_t Backplane::Create(RegisterWindowProviderInterface* register_window_provider,
                              std::unique_ptr<Backplane>* out_backplane) {
  zx_status_t status = ZX_OK;

  // Find what type of backplane we need to create.
  ChipIdRegister chip_id_register;
  {
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
    if ((status = register_window_provider->GetRegisterWindow(SI_ENUM_BASE, sizeof(ChipsetCoreRegs),
                                                              &register_window)) != ZX_OK) {
      BRCMF_ERR("Failed to get SI_ENUM_BASE window: %s", zx_status_get_string(status));
      return status;
    }
    if ((status = register_window->Read(chip_id_register.addr(), chip_id_register.mut_val())) !=
        ZX_OK) {
      BRCMF_ERR("Failed to read chip_id: %s", zx_status_get_string(status));
      return status;
    }
  }

  const CommonCoreId chip_id = static_cast<CommonCoreId>(chip_id_register.id());
  const uint16_t chip_rev = chip_id_register.rev();
  if (!IsSupportedCore(chip_id)) {
    BRCMF_ERR("Unsupported common core chip %d rev %d", static_cast<int>(chip_id),
              static_cast<int>(chip_rev));
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::unique_ptr<Backplane> backplane;
  switch (static_cast<ChipIdRegister::Type>(chip_id_register.type())) {
    default: {
      BRCMF_ERR("Invalid backplane type %d", static_cast<int>(chip_id_register.type()));
      return ZX_ERR_NOT_FOUND;
    }
  }

  *out_backplane = std::move(backplane);
  return ZX_OK;
}

CommonCoreId Backplane::chip_id() const { return chip_id_; }

uint16_t Backplane::chip_rev() const { return chip_rev_; }

}  // namespace brcmfmac
}  // namespace wlan

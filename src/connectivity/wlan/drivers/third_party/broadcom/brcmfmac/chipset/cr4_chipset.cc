// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/cr4_chipset.h"

#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/bitfield.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Core specific flag bits.
constexpr uint32_t kArmCr4IoctlCpuHalt = 0x0020;
constexpr uint32_t kD11IoctlPhyClockEn = 0x0004;
constexpr uint32_t kD11IoctlPhyReset = 0x0008;

class ArmCr4CapRegister : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x0004> {
 public:
  WLAN_BIT_FIELD(tcb_nab, 0, 4);
  WLAN_BIT_FIELD(tcb_nbb, 4, 4);
};

class ArmCr4BankIdxRegister : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x0040> {
 public:
  WLAN_BIT_FIELD(idx, 0, 32);
};

constexpr uint32_t kArmCr4BankInfoBanksizeMultiplier = 8192;

class ArmCr4BankInfoRegister
    : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x0044> {
 public:
  WLAN_BIT_FIELD(bsize, 0, 6);
};

class ArmCr4BankPdaRegister : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x004C> {
 public:
  WLAN_BIT_FIELD(pda, 0, 32);
};

uint32_t GetTcmRambase(CommonCoreId core_id) {
  switch (core_id) {
    case CommonCoreId::kBrcm4345:
      return 0x00198000;

    case CommonCoreId::kBrcm4335:
    case CommonCoreId::kBrcm4339:
    case CommonCoreId::kBrcm4350:
    case CommonCoreId::kBrcm4354:
    case CommonCoreId::kBrcm4356:
    case CommonCoreId::kBrcm43567:
    case CommonCoreId::kBrcm43569:
    case CommonCoreId::kBrcm43570:
    case CommonCoreId::kBrcm4358:
    case CommonCoreId::kBrcm43602:
    case CommonCoreId::kBrcm4371:
      return 0x00180000;

    case CommonCoreId::kBrcm4359:
      return 0x00160000;

    case CommonCoreId::kBrcm43465:
    case CommonCoreId::kBrcm43525:
    case CommonCoreId::kBrcm4365:
    case CommonCoreId::kBrcm4366:
      return 0x00200000;

    case CommonCoreId::kCypress4373:
      return 0x00160000;

    default:
      BRCMF_ERR("Invalid core id %d for TCM rambase", static_cast<int>(core_id));
      return 0;
  }
}

}  // namespace

Cr4Chipset::Cr4Chipset() = default;

Cr4Chipset::Cr4Chipset(Cr4Chipset&& other) { swap(*this, other); }

Cr4Chipset& Cr4Chipset::operator=(Cr4Chipset other) {
  swap(*this, other);
  return *this;
}

void swap(Cr4Chipset& lhs, Cr4Chipset& rhs) {
  using std::swap;
  swap(lhs.register_window_provider_, rhs.register_window_provider_);
  swap(lhs.backplane_, rhs.backplane_);
  swap(lhs.ramsize_, rhs.ramsize_);
}

Cr4Chipset::~Cr4Chipset() = default;

// static
zx_status_t Cr4Chipset::Create(RegisterWindowProviderInterface* register_window_provider,
                               Backplane* backplane, std::optional<Cr4Chipset>* out_chipset) {
  zx_status_t status = ZX_OK;

  // Put the chip into a passive state first.
  if ((status = backplane->ResetCore(Backplane::CoreId::kArmCr4Core, kArmCr4IoctlCpuHalt,
                                     kArmCr4IoctlCpuHalt)) != ZX_OK) {
    BRCMF_ERR("Failed to reset CR4 core: %s", zx_status_get_string(status));
    return status;
  }

  // fxb/29366: some chipsets don't reset the 80211 core.
  if (backplane->chip_id() != CommonCoreId::kBrcm4359) {
    if ((status = backplane->ResetCore(Backplane::CoreId::k80211Core,
                                       kD11IoctlPhyReset | kD11IoctlPhyClockEn,
                                       kD11IoctlPhyClockEn)) != ZX_OK) {
      BRCMF_ERR("Failed to reset 80211 core: %s", zx_status_get_string(status));
      return status;
    }
  }

  // Get the TCM RAM size.
  uint32_t ramsize = 0;
  {
    const auto core = backplane->GetCore(Backplane::CoreId::kArmCr4Core);
    if (core == nullptr) {
      BRCMF_ERR("Failed to get CR4 core info");
      return ZX_ERR_NOT_FOUND;
    }
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
    if ((status = register_window_provider->GetRegisterWindow(core->regbase, core->regsize,
                                                              &register_window)) != ZX_OK) {
      BRCMF_ERR("Failed to get CR4 core window: %s", zx_status_get_string(status));
      return status;
    }

    ArmCr4CapRegister cap_register;
    if ((status = register_window->Read(cap_register.addr(), cap_register.mut_val())) != ZX_OK) {
      BRCMF_ERR("Failed to read CR4 cap register: %s", zx_status_get_string(status));
      return status;
    }

    const uint32_t totb = cap_register.tcb_nab() + cap_register.tcb_nbb();
    for (uint32_t i = 0; i < totb; ++i) {
      ArmCr4BankIdxRegister bank_idx_register;
      bank_idx_register.set_idx(i);
      if ((status = register_window->Write(bank_idx_register.addr(), bank_idx_register.val())) !=
          ZX_OK) {
        BRCMF_ERR("Failed to write CR4 bank idx register: %s", zx_status_get_string(status));
        return status;
      }

      ArmCr4BankInfoRegister bank_info_register;
      if ((status = register_window->Read(bank_info_register.addr(),
                                          bank_info_register.mut_val())) != ZX_OK) {
        BRCMF_ERR("Failed to read CR4 bank info register: %s", zx_status_get_string(status));
        return status;
      }
      ramsize += (bank_info_register.bsize() + 1) * kArmCr4BankInfoBanksizeMultiplier;
    }
  }

  auto& chipset = out_chipset->emplace();
  chipset.register_window_provider_ = register_window_provider;
  chipset.backplane_ = backplane;
  chipset.ramsize_ = ramsize;
  return ZX_OK;
}

uint32_t Cr4Chipset::GetRambase() const { return GetTcmRambase(backplane_->chip_id()); }

size_t Cr4Chipset::GetRamsize() const { return ramsize_; }

zx_status_t Cr4Chipset::EnterUploadState() {
  zx_status_t status = ZX_OK;

  if (backplane_->chip_id() != CommonCoreId::kBrcm43602) {
    return ZX_OK;
  }

  // Enter the FW upload state.
  const auto core = backplane_->GetCore(Backplane::CoreId::kArmCr4Core);
  if (core == nullptr) {
    BRCMF_ERR("Failed to get CR4 core info");
    return ZX_ERR_NOT_FOUND;
  }
  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
  if ((status = register_window_provider_->GetRegisterWindow(core->regbase, core->regsize,
                                                             &register_window)) != ZX_OK) {
    BRCMF_ERR("Failed to get CR4 core window: %s", zx_status_get_string(status));
    return status;
  }

  constexpr int kIdxPda[][2] = {{5, 0}, {7, 0}};
  for (const auto& idx_pda : kIdxPda) {
    const int idx = idx_pda[0];
    const int pda = idx_pda[1];

    ArmCr4BankIdxRegister bank_idx;
    bank_idx.set_idx(idx);
    if ((status = register_window->Write(bank_idx.addr(), bank_idx.val())) != ZX_OK) {
      BRCMF_ERR("Failed to write CR4 bank idx register for idx %d pda %d: %s", idx, pda,
                zx_status_get_string(status));
      return status;
    }
    ArmCr4BankPdaRegister bank_pda;
    bank_pda.set_pda(pda);
    if ((status = register_window->Write(bank_pda.addr(), bank_pda.val())) != ZX_OK) {
      BRCMF_ERR("Failed to write CR4 bank PDA register for idx %d pda %d: %s", idx, pda,
                zx_status_get_string(status));
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Cr4Chipset::ExitUploadState() {
  zx_status_t status = ZX_OK;

  if (backplane_->chip_id() != CommonCoreId::kBrcm43602) {
    return ZX_OK;
  }

  // Exit FW upload state.
  if ((status = backplane_->ResetCore(Backplane::CoreId::kInternalMemCore, 0, 0)) != ZX_OK) {
    BRCMF_ERR("Failed to reset internal mem core: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t Cr4Chipset::Reset() {
  zx_status_t status = ZX_OK;
  if ((status = backplane_->ResetCore(Backplane::CoreId::kArmCr4Core, kArmCr4IoctlCpuHalt, 0)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to reset CR4 core: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan

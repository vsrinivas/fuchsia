// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/cr4_chipset.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include <optional>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/test/fake_register_window_provider_interface.h"

namespace wlan {
namespace brcmfmac {
namespace {

// A stub Backplane.
class StubBackplane : public Backplane {
 public:
  explicit StubBackplane(CommonCoreId core_id);
  ~StubBackplane() override;

  // Backplane implementation.
  const Backplane::Core* GetCore(Backplane::CoreId core_id) const override;
  zx_status_t IsCoreUp(Backplane::CoreId core_id, bool* out_is_up) override;
  zx_status_t DisableCore(Backplane::CoreId core_id, uint32_t prereset,
                          uint32_t postreset) override;
  zx_status_t ResetCore(Backplane::CoreId core_id, uint32_t prereset, uint32_t postreset) override;
};

StubBackplane::StubBackplane(CommonCoreId core_id) : Backplane(core_id, 0x01) {}

StubBackplane::~StubBackplane() = default;

const Backplane::Core* StubBackplane::GetCore(Backplane::CoreId core_id) const {
  if (core_id != Backplane::CoreId::kArmCr4Core) {
    return nullptr;
  }

  static const Backplane::Core kCoreInfo = {
      .id = Backplane::CoreId::kArmCr4Core,
      .rev = 0x01,
      .regbase = 0x10000,
      .regsize = 0x1000,
  };
  return &kCoreInfo;
}

zx_status_t StubBackplane::IsCoreUp(Backplane::CoreId core_id, bool* out_is_up) {
  *out_is_up = true;
  return ZX_OK;
}

zx_status_t StubBackplane::DisableCore(Backplane::CoreId core_id, uint32_t prereset,
                                       uint32_t postreset) {
  return ZX_OK;
}

zx_status_t StubBackplane::ResetCore(Backplane::CoreId core_id, uint32_t prereset,
                                     uint32_t postreset) {
  return ZX_OK;
}

TEST(Cr4ChipsetTest, CreationParameters) {
  FakeRegisterWindowProviderInterface register_window(0x100000, 0x1000);
  StubBackplane backplane(CommonCoreId::kBrcm43602);
  std::optional<Cr4Chipset> chipset;

  // Just try to create an instance.
  EXPECT_EQ(ZX_OK, Cr4Chipset::Create(&register_window, &backplane, &chipset));
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan

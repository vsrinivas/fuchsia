// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/axi_backplane.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include <iterator>
#include <optional>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/test/fake_register_window_provider_interface.h"

namespace wlan {
namespace brcmfmac {
namespace {

template <typename Iterator>
void FillErom(FakeRegisterWindowProviderInterface* register_window, Iterator begin, Iterator end) {
  // Set up the EROM table at offset 0x1000.
  register_window->Write(SI_ENUM_BASE + offsetof(ChipsetCoreRegs, eromptr), 0x1000);

  // Fill in the table.
  register_window->Fill(0x1000, begin, end);
}

TEST(AxiBackplaneTest, CreationParameters) {
  // A RegisterWindowProviderInterface that doesn't return any valid data will fail creation.
  {
    FakeRegisterWindowProviderInterface register_window(SI_ENUM_BASE + 0x1000, 0x1000);
    std::optional<AxiBackplane> backplane;
    EXPECT_NE(ZX_OK,
              AxiBackplane::Create(&register_window, CommonCoreId::kBrcm4359, 42, &backplane));
    EXPECT_FALSE(backplane.has_value());
  }

  // Create a trivial RegisterWindowProviderInterface with one core.
  {
    // Fill in some EROM values.
    FakeRegisterWindowProviderInterface register_window(SI_ENUM_BASE + 0x1000, 0x1000);
    constexpr uint32_t kRegbase = 0x20000;
    constexpr uint32_t kEromValues[] = {
        0x4bf80001, 0x33004411, 0x00000003, kRegbase | 0x5,
        0x1c000135, 0x01000000, 0x181000c5, 0x0000000f,
    };
    FillErom(&register_window, std::begin(kEromValues), std::end(kEromValues));

    std::optional<AxiBackplane> backplane;
    EXPECT_EQ(ZX_OK,
              AxiBackplane::Create(&register_window, CommonCoreId::kBrcm43465, 42, &backplane));
    EXPECT_TRUE(backplane.has_value());

    EXPECT_EQ(CommonCoreId::kBrcm43465, backplane->chip_id());
    EXPECT_EQ(42, backplane->chip_rev());

    const auto core = backplane->GetCore(Backplane::CoreId::kChipCommonCore);
    EXPECT_NE(nullptr, core);
    if (core != nullptr) {
      EXPECT_EQ(Backplane::CoreId::kChipCommonCore, core->id);
      EXPECT_EQ(0x33, core->rev);
      EXPECT_EQ(kRegbase, core->regbase);
      EXPECT_EQ(0x1000u, core->regsize);
    }
  }
}

TEST(AxiBackplaneTest, FullEromTable) {
  // Create a RegisterWindowProviderInterface with a real-world full EROM table.
  FakeRegisterWindowProviderInterface register_window(SI_ENUM_BASE + 0x1000, 0x1000);
  constexpr uint32_t kEromValues[] = {
      0x4bf80001, 0x33004411, 0x00000003, 0x18000005, 0x1c000135, 0x01000000, 0x181000c5,
      0x4bf81201, 0x36004211, 0x00000103, 0x18001005, 0x181010c5, 0x4bf83e01, 0x09084411,
      0x00000203, 0x18002005, 0x18005015, 0x00000135, 0x000a0000, 0x00180135, 0x00080000,
      0x00200135, 0x00040000, 0x181020c5, 0x18105185, 0x4bf83c01, 0x0e084411, 0x00000303,
      0x18003005, 0x08000135, 0x08000000, 0x0000013d, 0x80000000, 0x00000008, 0x80000000,
      0x181030c5, 0x18106185, 0x4bf82901, 0x15004211, 0x00000503, 0x18004005, 0x181040c5,
      0x43b13501, 0x00080201, 0x18000045, 0x18001045, 0x18002045, 0x18003045, 0x18004045,
      0x18005055, 0x18107085, 0x43b24001, 0x00080211, 0x00000603, 0x19000075, 0x01000000,
      0x18108085, 0x43b36701, 0x00000201, 0x18109005, 0x43b36601, 0x00000201, 0x1810a005,
      0x43b30101, 0x00000201, 0x18200035, 0x00100000, 0x43bfff01, 0x00080201, 0x000a0035,
      0x000e0000, 0x00240035, 0x07dc0000, 0x10000035, 0x08000000, 0x18008035, 0x000f8000,
      0x1810e035, 0x000f2000, 0x18300035, 0x00d00000, 0x1a000035, 0x02000000, 0x1d000035,
      0xe3000008, 0x7fffffff, 0x1810c085, 0x0000000f,
  };
  FillErom(&register_window, std::begin(kEromValues), std::end(kEromValues));

  std::optional<AxiBackplane> backplane;
  EXPECT_EQ(ZX_OK,
            AxiBackplane::Create(&register_window, CommonCoreId::kBrcm43465, 42, &backplane));
  EXPECT_TRUE(backplane.has_value());

  EXPECT_NE(nullptr, backplane->GetCore(Backplane::CoreId::kChipCommonCore));
  EXPECT_NE(nullptr, backplane->GetCore(Backplane::CoreId::k80211Core));
  EXPECT_NE(nullptr, backplane->GetCore(Backplane::CoreId::kArmCr4Core));
  EXPECT_NE(nullptr, backplane->GetCore(Backplane::CoreId::kPcie2Core));
  EXPECT_NE(nullptr, backplane->GetCore(Backplane::CoreId::kSdioDevCore));
}

TEST(AxiBackplaneTest, CoreOperations) {
  // Create a trivial RegisterWindowProviderInterface with one core.
  FakeRegisterWindowProviderInterface register_window(SI_ENUM_BASE + 0x1000, 0x1000);
  constexpr uint32_t kRegbase = 0x20000;
  constexpr uint32_t kWrapbase = 0x20000;
  constexpr uint32_t kEromValues[] = {
      0x4bf80001, 0x33004411, 0x00000003,       kRegbase | 0x05,
      0x1c000135, 0x01000000, kWrapbase | 0xc5, 0x0000000f,
  };
  FillErom(&register_window, std::begin(kEromValues), std::end(kEromValues));

  std::optional<AxiBackplane> backplane;
  EXPECT_EQ(ZX_OK,
            AxiBackplane::Create(&register_window, CommonCoreId::kBrcm43465, 42, &backplane));
  EXPECT_TRUE(backplane.has_value());
  const auto core = backplane->GetCore(Backplane::CoreId::kChipCommonCore);
  EXPECT_EQ(kRegbase, core->regbase);

  // The core and core reset control registers should all return 0 at this point, so the clock is
  // not up.
  constexpr uint32_t kCoreControlOffset = 0x0408;
  constexpr uint32_t kCoreResetControlOffset = 0x0800;
  bool is_up = false;
  EXPECT_EQ(ZX_OK, backplane->IsCoreUp(Backplane::CoreId::kChipCommonCore, &is_up));
  EXPECT_FALSE(is_up);

  // When the clock is up, the core is considered up.
  register_window.Write(kWrapbase + kCoreControlOffset, 0x00000001);
  EXPECT_EQ(ZX_OK, backplane->IsCoreUp(Backplane::CoreId::kChipCommonCore, &is_up));
  EXPECT_TRUE(is_up);

  // But if it is in reset, it is not up.
  register_window.Write(kWrapbase + kCoreResetControlOffset, 0x00000001);
  EXPECT_EQ(ZX_OK, backplane->IsCoreUp(Backplane::CoreId::kChipCommonCore, &is_up));
  EXPECT_FALSE(is_up);

  // Try disabling a core.  It should end up with the postreset vector we supply, and in reset.
  EXPECT_EQ(ZX_OK,
            backplane->DisableCore(Backplane::CoreId::kChipCommonCore, 0x01230000, 0x45670000));
  uint32_t value = 0;
  EXPECT_EQ(ZX_OK, register_window.Read(kWrapbase + kCoreControlOffset, &value));
  EXPECT_EQ(0x45670000u, value & 0xFFFFFFFC);
  EXPECT_EQ(0x3u, value & 0x3);

  // Now reset the core.  It should end up with a new postreset vector, and out of reset.
  EXPECT_EQ(ZX_OK,
            backplane->ResetCore(Backplane::CoreId::kChipCommonCore, 0x89AB0000, 0xCDEF0000));
  EXPECT_EQ(ZX_OK, register_window.Read(kWrapbase + kCoreControlOffset, &value));
  EXPECT_EQ(0xCDEF0000u, value & 0xFFFFFFFE);
  EXPECT_EQ(0x1u, value & 0x3);
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan

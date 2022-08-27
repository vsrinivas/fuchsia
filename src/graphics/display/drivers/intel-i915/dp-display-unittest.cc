// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/dp-display.h"

#include <lib/mmio-ptr/fake.h>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915/dpll.h"
#include "src/graphics/display/drivers/intel-i915/fake-dpcd-channel.h"
#include "src/graphics/display/drivers/intel-i915/intel-i915.h"
#include "src/graphics/display/drivers/intel-i915/pch-engine.h"
#include "src/graphics/display/drivers/intel-i915/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace {

// Value used to allocate space for the fake i915 register MMIO space.
// TODO(fxbug.dev/83998): Remove this once DpDisplay no longer depends on i915::Controller.
constexpr uint32_t kMmioSize = 0xd0000;

class TestDpll : public i915::DisplayPll {
 public:
  explicit TestDpll(registers::Dpll dpll) : DisplayPll(dpll) {}
  ~TestDpll() override = default;

  bool Enable(const i915::DpllState& state) final {
    enabled_ = true;
    return enabled_;
  }
  bool Disable() final {
    enabled_ = false;
    return enabled_;
  }

 private:
  bool enabled_ = false;
};

class TestDpllManager : public i915::DisplayPllManager {
 public:
  explicit TestDpllManager() {
    plls_.resize(kDplls.size());
    for (const auto dpll : kDplls) {
      plls_[dpll] = std::make_unique<TestDpll>(dpll);
      ref_count_[plls_[dpll].get()] = 0;
    }
  }

  std::optional<i915::DpllState> LoadState(registers::Ddi ddi) final {
    i915::DpllState state = i915::DpDpllState{
        .dp_bit_rate_mhz = 5400,
    };
    return std::make_optional(state);
  }

 private:
  constexpr static auto kDplls = {registers::Dpll::DPLL_0, registers::Dpll::DPLL_1,
                                  registers::Dpll::DPLL_2};

  bool MapImpl(registers::Ddi ddi, registers::Dpll dpll) final { return true; }
  bool UnmapImpl(registers::Ddi ddi) final { return true; }

  i915::DisplayPll* FindBestDpll(registers::Ddi ddi, bool is_edp,
                                 const i915::DpllState& state) final {
    for (const auto dpll : kDplls) {
      if (ref_count_[plls_[dpll].get()] == 0) {
        return plls_[dpll].get();
      }
    }
    return nullptr;
  }
};

class DpDisplayTest : public ::testing::Test {
 protected:
  DpDisplayTest()
      : controller_(nullptr),
        mmio_buffer_({
            .vaddr = FakeMmioPtr(buffer_),
            .offset = 0,
            .size = kMmioSize,
            .vmo = ZX_HANDLE_INVALID,
        }) {
    std::memset(buffer_, 0, sizeof(buffer_));
  }

  void SetUp() override {
    controller_.SetMmioForTesting(mmio_buffer_.View(0));
    controller_.SetDpllManagerForTesting(std::make_unique<TestDpllManager>());
    controller_.SetPowerWellForTesting(
        i915::Power::New(controller_.mmio_space(), i915::kTestDeviceDid));
    fake_dpcd_.SetDefaults();

    static constexpr int kAtlasGpuDeviceId = 0x591c;

    pch_engine_.emplace(controller_.mmio_space(), kAtlasGpuDeviceId);
    i915::PchClockParameters clock_parameters = pch_engine_->ClockParameters();
    pch_engine_->FixClockParameters(clock_parameters);
    pch_engine_->SetClockParameters(clock_parameters);
    i915::PchPanelParameters panel_parameters = pch_engine_->PanelParameters();
    panel_parameters.Fix();
    pch_engine_->SetPanelParameters(panel_parameters);
  }

  void TearDown() override {
    // Unset so controller teardown doesn't crash.
    controller_.ResetMmioSpaceForTesting();
  }

  std::unique_ptr<i915::DpDisplay> MakeDisplay(registers::Ddi ddi, uint64_t id = 1) {
    // TODO(fxbug.dev/86038): In normal operation a DpDisplay is not fully constructed until it
    // receives a call to DisplayDevice::Query, then either DisplayDevice::Init() (for a hotplug or
    // initially powered-off display) OR DisplayDevice::AttachPipe() and
    // DisplayDevice::LoadACtiveMode() (for a pre-initialized display, e.g. bootloader-configured
    // eDP). For testing we only initialize until the Query() stage. The states of a DpDisplay
    // should become easier to reason about if remove the partially-initialized states.
    auto display = std::make_unique<i915::DpDisplay>(&controller_, id, ddi, &fake_dpcd_,
                                                     &pch_engine_.value(), &node_);
    if (!display->Query()) {
      return nullptr;
    }
    return display;
  }

  i915::Controller* controller() { return &controller_; }
  i915::testing::FakeDpcdChannel* fake_dpcd() { return &fake_dpcd_; }
  i915::PchEngine* pch_engine() { return &pch_engine_.value(); }
  fdf::MmioBuffer* mmio_buffer() { return &mmio_buffer_; }

 private:
  // TODO(fxbug.dev/83998): Remove DpDisplay's dependency on i915::Controller which will remove the
  // need for much of what's in SetUp() and TearDown().
  i915::Controller controller_;
  uint8_t buffer_[kMmioSize];
  fdf::MmioBuffer mmio_buffer_;

  inspect::Node node_;
  i915::testing::FakeDpcdChannel fake_dpcd_;
  std::optional<i915::PchEngine> pch_engine_;
};

// Tests that display creation fails if the DP sink count is not 1, as MST is not supported.
TEST_F(DpDisplayTest, MultipleSinksNotSupported) {
  fake_dpcd()->SetSinkCount(2);
  ASSERT_EQ(nullptr, MakeDisplay(registers::DDI_A));
}

// Tests that the maximum supported lane count is 2 when DDI_A lane capability control is not
// supported.
TEST_F(DpDisplayTest, ReducedMaxLaneCountWhenDdiALaneCapControlNotSupported) {
  auto ddi_buf_ctl = registers::DdiRegs(registers::DDI_A).DdiBufControl().ReadFrom(mmio_buffer());
  ddi_buf_ctl.set_ddi_a_lane_capability_control(0);
  ddi_buf_ctl.WriteTo(mmio_buffer());

  fake_dpcd()->SetMaxLaneCount(4);

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_EQ(2, display->lane_count());
}

// Tests that the maximum supported lane count is selected when DDI_A lane capability control is
// supported.
TEST_F(DpDisplayTest, MaxLaneCount) {
  auto ddi_buf_ctl = registers::DdiRegs(registers::DDI_A).DdiBufControl().ReadFrom(mmio_buffer());
  ddi_buf_ctl.set_ddi_a_lane_capability_control(1);
  ddi_buf_ctl.WriteTo(mmio_buffer());

  fake_dpcd()->SetMaxLaneCount(4);

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_EQ(4, display->lane_count());
}

// Tests that the link rate is set to the maximum supported rate based on DPCD data upon
// initialization via Init().
TEST_F(DpDisplayTest, LinkRateSelectionViaInit) {
  // Set up the IGD, DPLL, panel power control, and DisplayPort lane status registers for
  // DpDisplay::Init() to succeed. Configuring the IGD op region to indicate eDP will cause
  // Controller to assign DPLL0 to the display.

  // TODO(fxbug.dev/83998): It shouldn't be necessary to rely on this logic in Controller to test
  // DpDisplay. Can DpDisplay be told that it is eDP during construction time instead of querying
  // Controller for it every time?
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(registers::DDI_A, true);
  auto dpll_status = registers::DpllStatus::Get().ReadFrom(mmio_buffer());
  dpll_status.set_reg_value(1u);
  dpll_status.WriteTo(mmio_buffer());

  // Mock the "Panel ready" status.
  auto panel_status = registers::PchPanelPowerStatus::Get().ReadFrom(mmio_buffer());
  panel_status.set_panel_on(1);
  panel_status.WriteTo(mmio_buffer());

  controller()->power()->SetDdiIoPowerState(registers::DDI_A, /* enable */ true);

  fake_dpcd()->registers[dpcd::DPCD_LANE0_1_STATUS] = 0xFF;
  fake_dpcd()->SetMaxLinkRate(dpcd::LinkBw::k5400Mbps);

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);

  EXPECT_TRUE(display->Init());
  EXPECT_EQ(5400u, display->link_rate_mhz());
}

// Tests that the link rate is set to a caller-assigned value upon initialization with
// InitWithDpllState.
TEST_F(DpDisplayTest, LinkRateSelectionViaInitWithDpllState) {
  // The max link rate should be disregarded by InitWithDpllState.
  fake_dpcd()->SetMaxLinkRate(dpcd::LinkBw::k5400Mbps);

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);

  i915::DpllState dpll_state = i915::DpDpllState{
      .dp_bit_rate_mhz = 4320u,
  };
  display->InitWithDpllState(&dpll_state);
  EXPECT_EQ(4320u, display->link_rate_mhz());
}

// Tests that the brightness value is obtained using the i915 south backlight control register
// when the related eDP DPCD capability is not supported.
TEST_F(DpDisplayTest, GetBacklightBrightnessUsesSouthBacklightRegister) {
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(registers::DDI_A, true);
  pch_engine()->SetPanelBrightness(0.5);

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_FLOAT_EQ(0.5, display->GetBacklightBrightness());
}

// Tests that the brightness value is obtained from the related eDP DPCD registers when supported.
TEST_F(DpDisplayTest, GetBacklightBrightnessUsesDpcd) {
  constexpr uint16_t kDpcdBrightness100 = 0xFFFF;
  constexpr uint16_t kDpcdBrightness20 = 0x3333;

  // Intentionally configure the PCH PWM brightness value to something different
  // to prove that the PCH backlight is not used.
  pch_engine()->SetPanelBrightness(0.5);
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(registers::DDI_A, true);

  fake_dpcd()->SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd()->SetEdpBacklightBrightnessCapable();

  // Set the brightness to 100%.
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_LSB] = kDpcdBrightness100 & 0xFF;
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_MSB] = kDpcdBrightness100 >> 8;

  auto display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_FLOAT_EQ(1.0, display->GetBacklightBrightness());

  // Set the brightness to 20%.
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_LSB] = kDpcdBrightness20 & 0xFF;
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_MSB] = kDpcdBrightness20 >> 8;

  display = MakeDisplay(registers::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_FLOAT_EQ(0.2, display->GetBacklightBrightness());
}

}  // namespace

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dp-display.h"

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>

#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-manager.h"
#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/fake-dpcd-channel.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pch-engine.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pipe-manager.h"
#include "src/graphics/display/drivers/intel-i915-tgl/power.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

// Value used to allocate space for the fake i915 register MMIO space.
// TODO(fxbug.dev/83998): Remove this once DpDisplay no longer depends on Controller.
constexpr uint32_t kMmioSize = 0xd0000;

class TestDpll : public DisplayPll {
 public:
  explicit TestDpll(tgl_registers::Dpll dpll) : DisplayPll(dpll) {}
  ~TestDpll() override = default;

  bool DoEnable(const DdiPllConfig& pll_config) final {
    enabled_ = true;
    return enabled_;
  }
  bool DoDisable() final {
    enabled_ = false;
    return enabled_;
  }

 private:
  bool enabled_ = false;
};

class TestDpllManager : public DisplayPllManager {
 public:
  explicit TestDpllManager() {
    for (const auto dpll : kDplls) {
      plls_[dpll] = std::make_unique<TestDpll>(dpll);
      ref_count_[plls_[dpll].get()] = 0;
    }
  }

  DdiPllConfig LoadState(DdiId ddi_id) final {
    return DdiPllConfig{
        .ddi_clock_khz = 2'700'000,  // DisplayPort HBR2 5.4Gbps / lane
        .spread_spectrum_clocking = false,
        .admits_display_port = true,
        .admits_hdmi = false,
    };
  }

 private:
  constexpr static auto kDplls = {tgl_registers::Dpll::DPLL_0, tgl_registers::Dpll::DPLL_1,
                                  tgl_registers::Dpll::DPLL_2};

  bool SetDdiClockSource(DdiId ddi_id, tgl_registers::Dpll pll) final { return true; }
  bool ResetDdiClockSource(DdiId ddi_id) final { return true; }

  DisplayPll* FindPllFor(DdiId ddi_id, bool is_edp, const DdiPllConfig& desired_config) final {
    for (const auto dpll : kDplls) {
      if (ref_count_[plls_[dpll].get()] == 0) {
        return plls_[dpll].get();
      }
    }
    return nullptr;
  }
};

class TestPipeManager : public PipeManager {
 public:
  explicit TestPipeManager(Controller* controller) : PipeManager(DefaultPipes(controller)) {}

  static std::vector<std::unique_ptr<Pipe>> DefaultPipes(Controller* controller) {
    std::vector<std::unique_ptr<Pipe>> pipes;
    pipes.push_back(std::make_unique<PipeSkylake>(controller->mmio_space(), tgl_registers::PIPE_A,
                                                  PowerWellRef{}));
    return pipes;
  }

  void ResetInactiveTranscoders() override {}

 private:
  Pipe* GetAvailablePipe() override { return At(tgl_registers::PIPE_A); }
  Pipe* GetPipeFromHwState(DdiId ddi_id, fdf::MmioBuffer* mmio_space) override {
    return At(tgl_registers::PIPE_A);
  }
};

class TestDdiPhysicalLayer final : public DdiPhysicalLayer {
 public:
  explicit TestDdiPhysicalLayer(DdiId ddi_id) : DdiPhysicalLayer(ddi_id) {}
  bool IsEnabled() const override { return enabled_; }
  bool IsHealthy() const override { return true; }
  bool Enable() override {
    enabled_ = true;
    return true;
  }
  bool Disable() override {
    enabled_ = false;
    return true;
  }

  PhysicalLayerInfo GetPhysicalLayerInfo() const override {
    return {
        .ddi_type = DdiPhysicalLayer::DdiType::kCombo,
        .connection_type = DdiPhysicalLayer::ConnectionType::kBuiltIn,
        .max_allowed_dp_lane_count = 4u,
    };
  }

 private:
  bool enabled_ = false;
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
    controller_.SetPipeManagerForTesting(std::make_unique<TestPipeManager>(controller()));
    controller_.SetPowerWellForTesting(Power::New(controller_.mmio_space(), kTestDeviceDid));
    fake_dpcd_.SetDefaults();

    static constexpr int kAtlasGpuDeviceId = 0x591c;

    pch_engine_.emplace(controller_.mmio_space(), kAtlasGpuDeviceId);
    PchClockParameters clock_parameters = pch_engine_->ClockParameters();
    pch_engine_->FixClockParameters(clock_parameters);
    pch_engine_->SetClockParameters(clock_parameters);
    PchPanelParameters panel_parameters = pch_engine_->PanelParameters();
    panel_parameters.Fix();
    pch_engine_->SetPanelParameters(panel_parameters);
  }

  void TearDown() override {
    // Unset so controller teardown doesn't crash.
    controller_.ResetMmioSpaceForTesting();
  }

  std::unique_ptr<DpDisplay> MakeDisplay(DdiId ddi_id, uint64_t id = 1) {
    // TODO(fxbug.dev/86038): In normal operation a DpDisplay is not fully constructed until it
    // receives a call to DisplayDevice::Query, then either DisplayDevice::Init() (for a hotplug or
    // initially powered-off display) OR DisplayDevice::AttachPipe() and
    // DisplayDevice::LoadACtiveMode() (for a pre-initialized display, e.g. bootloader-configured
    // eDP). For testing we only initialize until the Query() stage. The states of a DpDisplay
    // should become easier to reason about if remove the partially-initialized states.
    if (ddi_phys_[ddi_id] == nullptr) {
      ddi_phys_[ddi_id] = std::make_unique<TestDdiPhysicalLayer>(ddi_id);
      ddi_phys_[ddi_id]->Enable();
    }
    auto display =
        std::make_unique<DpDisplay>(&controller_, id, ddi_id, &fake_dpcd_, &pch_engine_.value(),
                                    DdiReference(ddi_phys_[ddi_id].get()), &node_);
    if (!display->Query()) {
      return nullptr;
    }
    return display;
  }

  Controller* controller() { return &controller_; }
  testing::FakeDpcdChannel* fake_dpcd() { return &fake_dpcd_; }
  PchEngine* pch_engine() { return &pch_engine_.value(); }
  fdf::MmioBuffer* mmio_buffer() { return &mmio_buffer_; }

 private:
  // TODO(fxbug.dev/83998): Remove DpDisplay's dependency on Controller which will remove
  // the need for much of what's in SetUp() and TearDown().
  Controller controller_;
  uint8_t buffer_[kMmioSize];
  fdf::MmioBuffer mmio_buffer_;

  inspect::Node node_;
  testing::FakeDpcdChannel fake_dpcd_;

  std::unordered_map<DdiId, std::unique_ptr<DdiPhysicalLayer>> ddi_phys_;

  std::optional<PchEngine> pch_engine_;
};

// Tests that display creation fails if the DP sink count is not 1, as MST is not supported.
TEST_F(DpDisplayTest, MultipleSinksNotSupported) {
  fake_dpcd()->SetSinkCount(2);
  ASSERT_EQ(nullptr, MakeDisplay(DdiId::DDI_A));
}

// Tests that the maximum supported lane count is 2 when DDI E is enabled.
TEST_F(DpDisplayTest, ReducedMaxLaneCountWhenDdiEIsEnabled) {
  auto buffer_control =
      tgl_registers::DdiRegs(DdiId::DDI_A).BufferControl().ReadFrom(mmio_buffer());
  buffer_control.set_ddi_e_disabled_kaby_lake(false).WriteTo(mmio_buffer());

  fake_dpcd()->SetMaxLaneCount(4);

  auto display = MakeDisplay(DdiId::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_EQ(2, display->lane_count());
}

// Tests that the maximum supported lane count is selected when DDI E is not enabled.
TEST_F(DpDisplayTest, MaxLaneCount) {
  auto buffer_control =
      tgl_registers::DdiRegs(DdiId::DDI_A).BufferControl().ReadFrom(mmio_buffer());
  buffer_control.set_ddi_e_disabled_kaby_lake(true).WriteTo(mmio_buffer());
  fake_dpcd()->SetMaxLaneCount(4);

  auto display = MakeDisplay(DdiId::DDI_A);
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
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(DdiId::DDI_A, true);
  auto dpll_status = tgl_registers::DisplayPllStatus::Get().ReadFrom(mmio_buffer());
  dpll_status.set_pll0_locked(true).WriteTo(mmio_buffer());

  // Mock the "Panel ready" status.
  auto panel_status = tgl_registers::PchPanelPowerStatus::Get().ReadFrom(mmio_buffer());
  panel_status.set_panel_on(1);
  panel_status.WriteTo(mmio_buffer());

  controller()->power()->SetDdiIoPowerState(DdiId::DDI_A, /* enable */ true);
  controller()->power()->SetAuxIoPowerState(DdiId::DDI_A, /* enable */ true);

  fake_dpcd()->registers[dpcd::DPCD_LANE0_1_STATUS] = 0xFF;
  fake_dpcd()->SetMaxLinkRate(dpcd::LinkBw::k5400Mbps);

  auto display = MakeDisplay(DdiId::DDI_A);
  ASSERT_NE(nullptr, display);

  EXPECT_TRUE(display->Init());
  EXPECT_EQ(5400u, display->link_rate_mhz());
}

// Tests that the link rate is set to a caller-assigned value upon initialization with
// InitWithDdiPllConfig.
TEST_F(DpDisplayTest, LinkRateSelectionViaInitWithDdiPllConfig) {
  // The max link rate should be disregarded by InitWithDdiPllConfig.
  fake_dpcd()->SetMaxLinkRate(dpcd::LinkBw::k5400Mbps);

  auto display = MakeDisplay(DdiId::DDI_A);
  ASSERT_NE(nullptr, display);

  const DdiPllConfig pll_config = {.ddi_clock_khz = 2'160'000,
                                   .spread_spectrum_clocking = false,
                                   .admits_display_port = true,
                                   .admits_hdmi = false};
  display->InitWithDdiPllConfig(pll_config);
  EXPECT_EQ(4320u, display->link_rate_mhz());
}

// Tests that the brightness value is obtained using the i915 south backlight control register
// when the related eDP DPCD capability is not supported.
TEST_F(DpDisplayTest, GetBacklightBrightnessUsesSouthBacklightRegister) {
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(DdiId::DDI_A, true);
  pch_engine()->SetPanelBrightness(0.5);

  auto display = MakeDisplay(DdiId::DDI_A);
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
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(DdiId::DDI_A, true);

  fake_dpcd()->SetEdpCapable(dpcd::EdpRevision::k1_4);
  fake_dpcd()->SetEdpBacklightBrightnessCapable();

  // Set the brightness to 100%.
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_LSB] = kDpcdBrightness100 & 0xFF;
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_MSB] = kDpcdBrightness100 >> 8;

  auto display = MakeDisplay(DdiId::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_FLOAT_EQ(1.0, display->GetBacklightBrightness());

  // Set the brightness to 20%.
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_LSB] = kDpcdBrightness20 & 0xFF;
  fake_dpcd()->registers[dpcd::DPCD_EDP_BACKLIGHT_BRIGHTNESS_MSB] = kDpcdBrightness20 >> 8;

  display = MakeDisplay(DdiId::DDI_A);
  ASSERT_NE(nullptr, display);
  EXPECT_FLOAT_EQ(0.2, display->GetBacklightBrightness());
}

}  // namespace

}  // namespace i915_tgl

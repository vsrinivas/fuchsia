// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pipe-manager.h"

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-manager.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"

namespace i915_tgl {

class PipeManagerTest : public ::testing::Test {
 public:
  PipeManagerTest() : controller_(nullptr) {}

  void SetUp() override {
    regs_.resize(kMinimumRegCount);
    reg_region_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.data(), sizeof(uint32_t),
                                                                kMinimumRegCount);
    mmio_buffer_.emplace(reg_region_->GetMmioBuffer());

    controller_.SetMmioForTesting(mmio_buffer_->View(0));
    controller_.SetPowerWellForTesting(Power::New(controller_.mmio_space(), kTestDeviceDid));
  }

  void TearDown() override {
    // Unset so controller teardown doesn't crash.
    controller_.ResetMmioSpaceForTesting();
  }

  Controller* controller() { return &controller_; }

 protected:
  constexpr static uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> reg_region_;
  std::vector<ddk_fake::FakeMmioReg> regs_;
  std::optional<fdf::MmioBuffer> mmio_buffer_;
  Controller controller_;
};

class FakeDisplay : public DisplayDevice {
 public:
  FakeDisplay(Controller* controller, uint64_t id, DdiId ddi_id, Type type)
      : DisplayDevice(controller, id, ddi_id, DdiReference(), type) {}
  ~FakeDisplay() override = default;

  // DisplayDevice overrides:
  bool Query() final { return true; }
  bool InitWithDdiPllConfig(const DdiPllConfig& pll_config) final { return true; }

 private:
  // DisplayDevice overrides:
  bool InitDdi() final { return true; }
  bool DdiModeset(const display_mode_t& mode) final { return true; }
  bool PipeConfigPreamble(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          TranscoderId transcoder_id) final {
    return true;
  }
  bool PipeConfigEpilogue(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          TranscoderId transcoder_id) final {
    return true;
  }
  DdiPllConfig ComputeDdiPllConfig(int32_t pixel_clock_10khz) final { return {}; }
  uint32_t LoadClockRateForTranscoder(TranscoderId transcoder_id) final { return 0; }
  uint32_t i2c_bus_id() const final { return 2 * ddi_id(); }
  bool CheckPixelRate(uint64_t pixel_rate) final { return true; }
};

// This tests if the PipeManager can allocate pipe for display devices and
// bind the display correctly.
TEST_F(PipeManagerTest, SkylakeAllocatePipe) {
  controller_.SetPipeManagerForTesting(std::make_unique<PipeManagerSkylake>(controller()));
  PipeManager* pm = controller_.pipe_manager();

  // Allocate pipe for DP display.
  uint64_t kDisplay1Id = 1u;
  std::unique_ptr<DisplayDevice> display1 = std::make_unique<FakeDisplay>(
      controller(), kDisplay1Id, DdiId::DDI_B, DisplayDevice::Type::kDp);
  Pipe* pipe1 = pm->RequestPipe(*display1);
  display1->set_pipe(pipe1);

  EXPECT_TRUE(pipe1);
  EXPECT_TRUE(pipe1->in_use());
  EXPECT_EQ(pipe1->attached_display_id(), kDisplay1Id);
  EXPECT_EQ(pipe1->tied_transcoder_id(), pipe1->connected_transcoder_id());

  // Allocate pipe for eDP display.
  controller()->igd_opregion_for_testing()->SetIsEdpForTesting(DdiId::DDI_A, true);

  uint64_t kDisplay2Id = 2u;
  std::unique_ptr<DisplayDevice> display2 = std::make_unique<FakeDisplay>(
      controller(), kDisplay2Id, DdiId::DDI_A, DisplayDevice::Type::kEdp);
  Pipe* pipe2 = pm->RequestPipe(*display2);
  display2->set_pipe(pipe2);

  EXPECT_TRUE(pipe2);
  EXPECT_NE(pipe2, pipe1);
  EXPECT_TRUE(pipe2->in_use());
  EXPECT_EQ(pipe2->attached_display_id(), kDisplay2Id);
  EXPECT_EQ(pipe2->connected_transcoder_id(), TranscoderId::TRANSCODER_EDP);

  display1.reset();
  EXPECT_FALSE(pipe1->in_use());

  display2.reset();
  EXPECT_FALSE(pipe2->in_use());
}

// This tests if the driver can reclaim used pipe and transcoder when the
// display is removed so that the pipes can be used for future devices.
TEST_F(PipeManagerTest, SkylakeReclaimUsedPipe) {
  controller_.SetPipeManagerForTesting(std::make_unique<PipeManagerSkylake>(controller()));
  PipeManager* pm = controller_.pipe_manager();

  for (size_t display_id = 1u;
       display_id <= tgl_registers::Pipes<tgl_registers::Platform::kKabyLake>().size() * 10;
       display_id++) {
    std::unique_ptr<DisplayDevice> display = std::make_unique<FakeDisplay>(
        controller(), display_id, DdiId::DDI_B, DisplayDevice::Type::kDp);
    Pipe* pipe = pm->RequestPipe(*display);
    display->set_pipe(pipe);

    EXPECT_TRUE(pipe);

    // On the end of each for loop, |display| is destroyed and |pipe| will be
    // reclaimed by PipeManager.
  }
}

}  // namespace i915_tgl

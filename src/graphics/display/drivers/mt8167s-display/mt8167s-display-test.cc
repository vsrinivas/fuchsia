// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "mt8167s-display.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fake-bti/bti.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mmio/mmio.h>
#include <lib/mock-sysmem/mock-buffer-collection.h>

#include <memory>

#include <ddktl/protocol/platform/device.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "lcd.h"
#include "mt-dsi-host.h"
#include "mt-sysconfig.h"

namespace sysmem = llcpp::fuchsia::sysmem;

namespace mt8167s_display {

namespace {
constexpr uint32_t kDsiHostRegNum = 132;
constexpr uint32_t kSyscfgRegNum = 336;
constexpr uint32_t kMutexRegNum = 48;

class MockNoCpuBufferCollection : public mock_sysmem::MockBufferCollection {
 public:
  MockNoCpuBufferCollection() { EXPECT_EQ(ZX_OK, fake_bti_create(bti_.reset_and_get_address())); }
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_FALSE(constraints.buffer_memory_constraints.cpu_domain_supported);
    image_constraints_ = constraints.image_format_constraints[0];
    set_constraints_called_ = true;
  }

  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& completer) override {
    sysmem::BufferCollectionInfo_2 info;
    zx::vmo vmo;
    constexpr uint32_t kWidth = 800;
    constexpr uint32_t kHeight = 600;
    ASSERT_OK(zx::vmo::create_contiguous(bti_, kWidth * kHeight * 4, 0u, &vmo));
    info.buffers[0].vmo = std::move(vmo);
    info.buffer_count = 1;
    info.settings.has_image_format_constraints = true;
    info.settings.image_format_constraints = image_constraints_;
    info.settings.image_format_constraints.max_coded_width = kWidth;
    info.settings.image_format_constraints.max_coded_height = kHeight;
    info.settings.image_format_constraints.max_bytes_per_row = kWidth * 4;
    completer.Reply(ZX_OK, std::move(info));
  }

  bool set_constraints_called() const { return set_constraints_called_; }

 private:
  bool set_constraints_called_ = false;
  sysmem::ImageFormatConstraints image_constraints_;
  zx::bti bti_;
};

}  // namespace

/********************************/
/* LCD Unit Tests */
/********************************/

TEST(LcdTest, PowerOn) {
  // get the dsi and gpio protocols
  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  Lcd lcd(&dsi, &gpio, 0);
  lcd.PowerOn();
}

TEST(LcdTest, PowerOff) {
  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  Lcd lcd(&dsi, &gpio, 0);
  lcd.PowerOff();
}

TEST(DsiHostTest, IsDsiHostOn) {
  pdev_protocol_t pdev = {};
  MtDsiHost dsi_host(&pdev, 0, 0, 0);

  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
  fbl::AllocChecker ac;
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, &dsi, &gpio, uint8_t(0));
  EXPECT_TRUE(ac.check());
  ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
  ddk_mock::MockMmioRegRegion mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
  std::unique_ptr<ddk::MmioBuffer> mmio;
  mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());
  // This will simulate the HOST being ON
  mmio->Write32(0x1, 0x50);
  EXPECT_OK(dsi_host.Init(std::move(mmio), std::move(lcd), &dsi, &gpio, &power));
  EXPECT_TRUE(dsi_host.IsHostOn());
}

TEST(DsiHostTest, IsDsiHostOff) {
  pdev_protocol_t pdev = {};
  MtDsiHost dsi_host(&pdev, 0, 0, 0);
  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
  fbl::AllocChecker ac;
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, &dsi, &gpio, uint8_t(0));
  EXPECT_TRUE(ac.check());
  ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
  ddk_mock::MockMmioRegRegion mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
  std::unique_ptr<ddk::MmioBuffer> mmio;
  mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());
  // This will simulate the HOST being OFF
  mmio->Write32(0x0, 0x50);
  EXPECT_OK(dsi_host.Init(std::move(mmio), std::move(lcd), &dsi, &gpio, &power));
  EXPECT_FALSE(dsi_host.IsHostOn());
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already off */
TEST(DsiHostTest, DsiHostShutdown_OFF) {
  pdev_protocol_t pdev = {};
  MtDsiHost dsi_host(&pdev, 0, 0, 0);

  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
  fbl::AllocChecker ac;
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, &dsi, &gpio, uint8_t(0));
  EXPECT_TRUE(ac.check());
  ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
  ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
  std::unique_ptr<ddk::MmioBuffer> dsi_mmio;
  dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  // This will simulate the HOST being OFF
  dsi_mmio->Write32(0x0, 0x50);
  EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

  ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
  ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
  std::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
  syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
  ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
  std::unique_ptr<ddk::MmioBuffer> mutex_mmio;
  mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  std::unique_ptr<MtSysConfig> syscfg;
  syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
  EXPECT_TRUE(ac.check());
  EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
  EXPECT_OK(dsi_host.Shutdown(syscfg));
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already ON */
TEST(DsiHostTest, DsiHostShutdown_ON) {
  pdev_protocol_t pdev = {};
  MtDsiHost dsi_host(&pdev, 0, 0, 0);

  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
  fbl::AllocChecker ac;
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, &dsi, &gpio, uint8_t(0));
  EXPECT_TRUE(ac.check());

  ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
  ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
  std::unique_ptr<ddk::MmioBuffer> dsi_mmio;
  dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  // This will simulate the HOST being OFF
  dsi_mmio->Write32(0x1, 0x50);
  EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

  ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
  ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
  std::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
  syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
  ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
  std::unique_ptr<ddk::MmioBuffer> mutex_mmio;
  mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  std::unique_ptr<MtSysConfig> syscfg;
  syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
  EXPECT_TRUE(ac.check());
  EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
  EXPECT_OK(dsi_host.Shutdown(syscfg));
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already off */
TEST(DsiHostTest, DsiHostPowerOn) {
  pdev_protocol_t pdev = {};
  MtDsiHost dsi_host(&pdev, 0, 0, 0);

  ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
  ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
  ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
  fbl::AllocChecker ac;
  std::unique_ptr<Lcd> lcd =
      fbl::make_unique_checked<mt8167s_display::Lcd>(&ac, &dsi, &gpio, uint8_t(0));
  EXPECT_TRUE(ac.check());

  ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
  ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
  std::unique_ptr<ddk::MmioBuffer> dsi_mmio;
  dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  // This will simulate the HOST being OFF
  dsi_mmio->Write32(0x0, 0x50);
  EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

  ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
  ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
  std::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
  syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
  ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
  std::unique_ptr<ddk::MmioBuffer> mutex_mmio;
  mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
  EXPECT_TRUE(ac.check());

  std::unique_ptr<MtSysConfig> syscfg;
  syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
  EXPECT_TRUE(ac.check());
  EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
  EXPECT_OK(dsi_host.PowerOn(syscfg));
}

TEST(DisplayTest, SetConstraints) {
  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  mt8167s_display::Mt8167sDisplay display(nullptr);
  display.SetBtiForTesting(std::move(bti));

  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockNoCpuBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  loop.StartThread();

  image_t image = {};
  image.width = 800;
  image.height = 600;
  image.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  EXPECT_OK(display.DisplayControllerImplImportImage(&image, client_channel.get(), 0));
  // Sync calls should ensure set_constraints will be called at the right point.
  EXPECT_TRUE(collection.set_constraints_called());
}

}  // namespace mt8167s_display

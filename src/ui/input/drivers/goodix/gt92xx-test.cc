// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gt92xx.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/ddk/metadata.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <atomic>

#include <ddk/metadata/buttons.h>
#include <hid/gt92xx.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace goodix {

class Gt92xxTest : public Gt92xxDevice {
 public:
  Gt92xxTest(ddk::I2cChannel i2c, ddk::GpioProtocolClient intr, ddk::GpioProtocolClient reset,
             zx_device_t* parent)
      : Gt92xxDevice(parent, i2c, intr, reset) {}

  void Running(bool run) { Gt92xxDevice::running_.store(run); }

  zx_status_t Init() { return Gt92xxDevice::Init(); }

  void Trigger() { irq_.trigger(0, zx::time()); }
  zx_status_t StartThread() {
    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));

    auto thunk = [](void* arg) -> int { return reinterpret_cast<Gt92xxTest*>(arg)->Thread(); };

    Running(true);
    int ret = thrd_create_with_name(&test_thread_, thunk, this, "gt92xx-test-thread");
    return (ret == thrd_success) ? ZX_OK : ZX_ERR_BAD_STATE;
  }
  zx_status_t StopThread() {
    Running(false);
    irq_.trigger(0, zx::time());
    int ret = thrd_join(test_thread_, NULL);
    return (ret == thrd_success) ? ZX_OK : ZX_ERR_BAD_STATE;
  }

  thrd_t test_thread_;
};

static std::atomic<uint8_t> rpt_ran = 0;

void rpt_handler(void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
  gt92xx_touch_t touch_rpt = {};
  touch_rpt.rpt_id = GT92XX_RPT_ID_TOUCH;
  touch_rpt.fingers[0] = {0x01, 0x110, 0x100};
  touch_rpt.fingers[1] = {0x05, 0x220, 0x200};
  touch_rpt.fingers[2] = {0x09, 0x330, 0x300};
  touch_rpt.fingers[3] = {0x0d, 0x440, 0x400};
  touch_rpt.fingers[4] = {0x11, 0x550, 0x500};
  touch_rpt.contact_count = 5;
  ASSERT_BYTES_EQ(buffer, &touch_rpt, size);
  EXPECT_EQ(size, sizeof(touch_rpt));
  rpt_ran.store(1);
}

TEST(GoodixTest, Init) {
  ddk::MockGpio reset_mock;
  ddk::MockGpio intr_mock;
  mock_i2c::MockI2c mock_i2c;
  zx::interrupt irq;

  reset_mock.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  intr_mock.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  const gpio_protocol_t* reset = reset_mock.GetProto();
  const gpio_protocol_t* intr = intr_mock.GetProto();

  ddk::I2cChannel i2c(mock_i2c.GetProto());

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c, intr, reset, fake_parent.get());

  mock_i2c
      .ExpectWrite({static_cast<uint8_t>(GT_REG_CONFIG_DATA >> 8),
                    static_cast<uint8_t>(GT_REG_CONFIG_DATA & 0xff)})
      .ExpectReadStop({0x00})
      .ExpectWriteStop(Gt92xxDevice::GetConfData())
      .ExpectWriteStop({static_cast<uint8_t>(GT_REG_TOUCH_STATUS >> 8),
                        static_cast<uint8_t>(GT_REG_TOUCH_STATUS & 0xff), 0x00})
      .ExpectWrite({static_cast<uint8_t>(GT_REG_CONFIG_DATA >> 8),
                    static_cast<uint8_t>(GT_REG_CONFIG_DATA & 0xff)})
      .ExpectReadStop({0x00})
      .ExpectWrite({static_cast<uint8_t>(GT_REG_FW_VERSION >> 8),
                    static_cast<uint8_t>(GT_REG_FW_VERSION & 0xff)})
      .ExpectReadStop({0x05, 0x61});

  EXPECT_OK(device.Init());
  ASSERT_NO_FATAL_FAILURES(reset_mock.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr_mock.VerifyAndClear());
}

TEST(GoodixTest, InitForceConfig) {
  ddk::MockGpio reset_mock;
  ddk::MockGpio intr_mock;
  mock_i2c::MockI2c mock_i2c;
  zx::interrupt irq;

  reset_mock.ExpectConfigOut(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  intr_mock.ExpectConfigOut(ZX_OK, 0)
      .ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  const gpio_protocol_t* reset = reset_mock.GetProto();
  const gpio_protocol_t* intr = intr_mock.GetProto();

  ddk::I2cChannel i2c(mock_i2c.GetProto());

  auto fake_parent = MockDevice::FakeRootParent();
  Gt92xxTest device(i2c, intr, reset, fake_parent.get());

  fbl::Vector conf_data = Gt92xxDevice::GetConfData();
  EXPECT_NE(conf_data[sizeof(uint16_t)], 0x00);
  conf_data[sizeof(uint16_t)] = 0x00;

  mock_i2c
      .ExpectWrite({static_cast<uint8_t>(GT_REG_CONFIG_DATA >> 8),
                    static_cast<uint8_t>(GT_REG_CONFIG_DATA & 0xff)})
      .ExpectReadStop({0x60})
      .ExpectWriteStop(std::move(conf_data))
      .ExpectWriteStop({static_cast<uint8_t>(GT_REG_TOUCH_STATUS >> 8),
                        static_cast<uint8_t>(GT_REG_TOUCH_STATUS & 0xff), 0x00})
      .ExpectWrite({static_cast<uint8_t>(GT_REG_CONFIG_DATA >> 8),
                    static_cast<uint8_t>(GT_REG_CONFIG_DATA & 0xff)})
      .ExpectReadStop({0x60})
      .ExpectWrite({static_cast<uint8_t>(GT_REG_FW_VERSION >> 8),
                    static_cast<uint8_t>(GT_REG_FW_VERSION & 0xff)})
      .ExpectReadStop({0x05, 0x61});

  EXPECT_OK(device.Init());
  ASSERT_NO_FATAL_FAILURES(reset_mock.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(intr_mock.VerifyAndClear());
}

TEST(GoodixTest, TestReport) {
  ddk::MockGpio reset_mock;
  ddk::MockGpio intr_mock;
  mock_i2c::MockI2c mock_i2c;

  mock_i2c
      .ExpectWrite({static_cast<uint8_t>(GT_REG_TOUCH_STATUS >> 8),
                    static_cast<uint8_t>(GT_REG_TOUCH_STATUS & 0xff)})
      .ExpectReadStop({0x85})
      .ExpectWrite(
          {static_cast<uint8_t>(GT_REG_REPORTS >> 8), static_cast<uint8_t>(GT_REG_REPORTS & 0xff)})
      .ExpectReadStop({0x00, 0x00, 0x01, 0x10, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00,
                       0x02, 0x20, 0x02, 0x01, 0x01, 0x00, 0x02, 0x00, 0x03, 0x30,
                       0x03, 0x01, 0x01, 0x00, 0x03, 0x00, 0x04, 0x40, 0x04, 0x01,
                       0x01, 0x00, 0x04, 0x00, 0x05, 0x50, 0x05, 0x01, 0x01, 0x00})
      .ExpectWriteStop({static_cast<uint8_t>(GT_REG_TOUCH_STATUS >> 8),
                        static_cast<uint8_t>(GT_REG_TOUCH_STATUS & 0xff), 0x00});

  ddk::I2cChannel i2c(mock_i2c.GetProto());

  auto fake_parent = MockDevice::FakeRootParent();\
  Gt92xxTest device(i2c, intr_mock.GetProto(), reset_mock.GetProto(), fake_parent.get());
  EXPECT_OK(device.StartThread());
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = rpt_handler;

  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  device.Trigger();
  while (!rpt_ran.load()) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  }
  EXPECT_OK(device.StopThread());
}

}  // namespace goodix

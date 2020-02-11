
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cy8cmbr3108.h"

#include <lib/fake-hidbus-ifc/fake-hidbus-ifc.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <ddk/metadata.h>
#include <ddktl/protocol/gpio.h>
#include <hid/visalia-touch.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace {
static const touch_button_config_t touch_buttons[] = {
    {
        .id = BUTTONS_ID_VOLUME_UP,
        .idx = 4,
    },
    {
        .id = BUTTONS_ID_VOLUME_DOWN,
        .idx = 5,
    },
    {
        .id = BUTTONS_ID_PLAY_PAUSE,
        .idx = 0,
    },
};
}  // namespace

namespace cypress {
class Cy8cmbr3108Test : public Cy8cmbr3108 {
 public:
  Cy8cmbr3108Test() : Cy8cmbr3108(nullptr) {}

  zx_status_t Init() {
    zx::interrupt dup_irq;
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &mock_irq_);
    mock_irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_irq);

    mock_touch_gpio_.ExpectSetAltFunction(ZX_OK, 0)
        .ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
        .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, std::move(dup_irq))
        .ExpectReleaseInterrupt(ZX_OK);

    return Cy8cmbr3108::Init();
  }

  zx_status_t InitializeProtocols() override {
    auto gpio_proto = ddk::GpioProtocolClient(mock_touch_gpio_.GetProto());
    touch_gpio_ = std::move(gpio_proto);

    auto i2c_proto = ddk::I2cProtocolClient(mock_i2c_.GetProto());
    i2c_ = std::move(i2c_proto);

    const size_t n_buttons = countof(touch_buttons);
    buttons_ = fbl::Array(new touch_button_config_t[n_buttons], n_buttons);
    for (uint32_t i = 0; i < n_buttons; i++) {
      buttons_[i] = touch_buttons[i];
    }
    return ZX_OK;
  }

  void VerifyAll() {
    mock_touch_gpio_.VerifyAndClear();
    mock_i2c_.VerifyAndClear();
  }

  void FakeInterrupt() { mock_irq_.trigger(0, zx::time()); }

  mock_i2c::MockI2c& mock_i2c() { return mock_i2c_; }

 private:
  ddk::MockGpio mock_touch_gpio_;
  mock_i2c::MockI2c mock_i2c_;
  zx::interrupt mock_irq_;
};

TEST(Cy8cmbr3108Test, Init) {
  Cy8cmbr3108Test dut;
  dut.Init();
  dut.ShutDown();
  dut.VerifyAll();
}

TEST(Cy8cmbr3108Test, ButtonTouched) {
  Cy8cmbr3108Test dut;
  dut.Init();

  visalia_touch_buttons_input_rpt_t expected_rpt = {};
  expected_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;
  expected_rpt.volume_up = 1;

  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus;
  dut.HidbusStart(fake_hid_bus.GetProto());

  dut.mock_i2c().ExpectWrite({0xAA}).ExpectReadStop({0x10, 0x00});
  dut.FakeInterrupt();

  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus.WaitUntilNextReport(&returned_rpt));
  EXPECT_EQ(returned_rpt.size(), sizeof(expected_rpt));
  ASSERT_BYTES_EQ(returned_rpt.data(), &expected_rpt, returned_rpt.size());

  dut.ShutDown();
  dut.VerifyAll();
}

TEST(Cy8cmbr3108Test, ButtonReleased) {
  Cy8cmbr3108Test dut;
  dut.Init();

  visalia_touch_buttons_input_rpt_t expected_rpt = {};
  expected_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;

  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus;
  dut.HidbusStart(fake_hid_bus.GetProto());

  dut.mock_i2c().ExpectWrite({0xAA}).ExpectReadStop({0x00, 0x00});
  dut.FakeInterrupt();

  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus.WaitUntilNextReport(&returned_rpt));
  EXPECT_EQ(returned_rpt.size(), sizeof(expected_rpt));
  ASSERT_BYTES_EQ(returned_rpt.data(), &expected_rpt, returned_rpt.size());

  dut.ShutDown();
  dut.VerifyAll();
}

TEST(Cy8cmbr3108Test, MultipleButtonTouch) {
  Cy8cmbr3108Test dut;
  dut.Init();

  visalia_touch_buttons_input_rpt_t expected_rpt = {};
  expected_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;
  expected_rpt.volume_down = 1;
  expected_rpt.pause = 1;

  fake_hidbus_ifc::FakeHidbusIfc fake_hid_bus;
  dut.HidbusStart(fake_hid_bus.GetProto());

  dut.mock_i2c().ExpectWrite({0xAA}).ExpectReadStop({0x21, 0x00});
  dut.FakeInterrupt();

  std::vector<uint8_t> returned_rpt;
  ASSERT_OK(fake_hid_bus.WaitUntilNextReport(&returned_rpt));
  EXPECT_EQ(returned_rpt.size(), sizeof(expected_rpt));
  ASSERT_BYTES_EQ(returned_rpt.data(), &expected_rpt, returned_rpt.size());

  dut.ShutDown();
  dut.VerifyAll();
}

}  // namespace cypress

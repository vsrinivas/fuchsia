// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-buttons.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <sys/types.h>

#include <cstddef>

#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "ddk/protocol/buttons.h"
#include "zircon/assert.h"

namespace {
static const buttons_button_config_t buttons_direct[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
};

static const buttons_gpio_config_t gpios_direct[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}}};

static const buttons_button_config_t buttons_multiple[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 1, 0, 0},
};

static const buttons_gpio_config_t gpios_multiple[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
};

static const buttons_button_config_t buttons_matrix[] = {
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP, 0, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A, 1, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M, 0, 3, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
};

static const buttons_gpio_config_t gpios_matrix[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_PULL_UP}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_PULL_UP}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {0}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {0}},
};
}  // namespace

namespace buttons {

enum class TestType {
  kTestDirect,
  kTestMatrix,
  kTestNotify,
};

class HidButtonsDeviceTest : public HidButtonsDevice {
 public:
  HidButtonsDeviceTest(ddk::MockGpio* gpios, size_t gpios_count, TestType type)
      : HidButtonsDevice(fake_ddk::kFakeParent), type_(type) {
    fbl::AllocChecker ac;
    using MockPointer = ddk::MockGpio*;
    gpio_mocks_.reset(new (&ac) MockPointer[gpios_count], gpios_count);
    for (size_t i = 0; i < gpios_count; ++i) {
      gpio_mocks_[i] = &gpios[i];
    }
  }

  void DdkUnbind() { HidButtonsDevice::ShutDown(); DdkRemove(); }
  void DdkRelease() { delete this; }

  void ShutDownTest() { DdkUnbind(); }

  void SetupGpio(zx::interrupt irq, size_t gpio_index) {
    auto* mock = gpio_mocks_[gpio_index];
    mock->ExpectSetAltFunction(ZX_OK, 0);
    switch (type_) {
      case TestType::kTestDirect:
        mock->ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
            .ExpectRead(ZX_OK, 0)  // Not pushed, low.
            .ExpectReleaseInterrupt(ZX_OK)
            .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, std::move(irq));

        // Make sure polarity is correct in case it changed during configuration.
        mock->ExpectRead(ZX_OK, 0)                         // Not pushed.
            .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Set correct polarity.
            .ExpectRead(ZX_OK, 0);                         // Still not pushed.
        break;
      case TestType::kTestMatrix:
        if (gpios_matrix[gpio_index].type == BUTTONS_GPIO_TYPE_INTERRUPT) {
          mock->ExpectConfigIn(ZX_OK, gpios_matrix[gpio_index].internal_pull)
              .ExpectRead(ZX_OK, 0)  // Not pushed, low.
              .ExpectReleaseInterrupt(ZX_OK)
              .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, std::move(irq));

          // Make sure polarity is correct in case it changed during configuration.
          mock->ExpectRead(ZX_OK, 0)                         // Not pushed.
              .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Set correct polarity.
              .ExpectRead(ZX_OK, 0);                         // Still not pushed.
        } else {
          mock->ExpectConfigOut(ZX_OK, gpios_matrix[gpio_index].output_value);
        }
        break;
      case TestType::kTestNotify:
        mock->ExpectConfigIn(ZX_OK, gpios_multiple[gpio_index].internal_pull)
            .ExpectRead(ZX_OK, 0)  // Not pushed, low.
            .ExpectReleaseInterrupt(ZX_OK)
            .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, std::move(irq));

        // Make sure polarity is correct in case it changed during configuration.
        mock->ExpectRead(ZX_OK, 0)                         // Not pushed.
            .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Set correct polarity.
            .ExpectRead(ZX_OK, 0);                         // Still not pushed.
        break;
    }
  }

  zx_status_t BindTest() {
    const size_t n_gpios = gpio_mocks_.size();
    auto gpios = fbl::Array(new HidButtonsDevice::Gpio[n_gpios], n_gpios);
    for (size_t i = 0; i < n_gpios; ++i) {
      gpios[i].gpio = *gpio_mocks_[i]->GetProto();
    }
    switch (type_) {
      case TestType::kTestDirect: {
        for (size_t i = 0; i < n_gpios; ++i) {
          gpios[i].config = gpios_direct[i];
        }
        constexpr size_t n_buttons = countof(buttons_direct);
        auto buttons = fbl::Array(new buttons_button_config_t[n_buttons], n_buttons);
        for (size_t i = 0; i < n_buttons; ++i) {
          buttons[i] = buttons_direct[i];
        }
        return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
      }
      case TestType::kTestMatrix: {
        for (size_t i = 0; i < n_gpios; ++i) {
          gpios[i].config = gpios_matrix[i];
        }
        constexpr size_t n_buttons = countof(buttons_matrix);
        auto buttons = fbl::Array(new buttons_button_config_t[n_buttons], n_buttons);
        for (size_t i = 0; i < n_buttons; ++i) {
          buttons[i] = buttons_matrix[i];
        }
        return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
      }
      case TestType::kTestNotify: {
        for (size_t i = 0; i < n_gpios; ++i) {
          gpios[i].config = gpios_multiple[i];
        }
        constexpr size_t n_buttons = countof(buttons_multiple);
        auto buttons = fbl::Array(new buttons_button_config_t[n_buttons], n_buttons);
        for (size_t i = 0; i < n_buttons; ++i) {
          buttons[i] = buttons_multiple[i];
        }
        return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
      }
    }
    return ZX_OK;
  }

  void UnregisterTest() {
    fbl::AutoLock lock(&callbacks_lock_);
    for (uint32_t i = 0; i < BUTTON_TYPE_MAX; i++) {
      EXPECT_EQ(callbacks_[i].size(), 0);
    }
  }

  void FakeInterrupt() {
    // Issue the first interrupt.
    zx_port_packet packet = {kPortKeyInterruptStart + 0, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

  void FakeInterrupt(button_type_t type) {
    // Issue the first interrupt.
    zx_port_packet packet = {
        kPortKeyInterruptStart + button_map_[type], ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

 private:
  TestType type_;
  fbl::Array<ddk::MockGpio*> gpio_mocks_;
};

TEST(HidButtonsTest, DirectButtonBind) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  EXPECT_OK(device.BindTest());
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPush) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  EXPECT_OK(device.BindTest());
  device.FakeInterrupt();
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonUnpushedReport) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  EXPECT_OK(device.BindTest());
  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 0;  // Unpushed.
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);
  device.FakeInterrupt();
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushedReport) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  EXPECT_OK(device.BindTest());
  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 1;  // Pushed
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);
  device.FakeInterrupt();
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushUnpushPush) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  EXPECT_OK(device.BindTest());
  device.FakeInterrupt();
  device.FakeInterrupt();
  device.FakeInterrupt();
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonFlaky) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  // Reconfigure Polarity due to interrupt and keep checking until correct.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                          // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)   // Turn the polarity.
      .ExpectRead(ZX_OK, 0)                          // Oops now not pushed! not ok, retry.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                          // Oops pushed! not ok, retry.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)   // Turn the polarity.
      .ExpectRead(ZX_OK, 0)                          // Oops now not pushed! not ok, retry.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                          // Oops pushed again! not ok, retry.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)   // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                         // Now pushed and polarity set low, ok.
  // Read value to generate report.
  mock_gpios[0].ExpectRead(ZX_OK, 1);  // Pushed.

  EXPECT_OK(device.BindTest());
  device.FakeInterrupt();
  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, MatrixButtonBind) {
  ddk::MockGpio mock_gpios[countof(gpios_matrix)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestMatrix);
  zx::interrupt irqs[countof(gpios_matrix)];
  for (size_t i = 0; i < countof(gpios_matrix); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_matrix); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, MatrixButtonPush) {
  ddk::MockGpio mock_gpios[countof(gpios_matrix)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestMatrix);
  zx::interrupt irqs[countof(gpios_matrix)];
  for (size_t i = 0; i < countof(gpios_matrix); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.

  // Matrix Scan for 0.
  mock_gpios[2].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                   // Float column.
  mock_gpios[0].ExpectRead(ZX_OK, 1);                                  // Read row.
  mock_gpios[2].ExpectConfigOut(ZX_OK, gpios_matrix[2].output_value);  // Restore column.

  // Matrix Scan for 1.
  mock_gpios[2].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                   // Float column.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                                  // Read row.
  mock_gpios[2].ExpectConfigOut(ZX_OK, gpios_matrix[2].output_value);  // Restore column.

  // Matrix Scan for 2.
  mock_gpios[3].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                   // Float column.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                                  // Read row.
  mock_gpios[3].ExpectConfigOut(ZX_OK, gpios_matrix[3].output_value);  // Restore column.

  // Matrix Scan for 3.
  mock_gpios[3].ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                   // Float column.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                                  // Read row.
  mock_gpios[3].ExpectConfigOut(ZX_OK, gpios_matrix[3].output_value);  // Restore colument.

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 1;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);
  device.FakeInterrupt();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_matrix); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

class FakeNotifyDevice {
 public:
  FakeNotifyDevice(bool val) { val_ = val; }
  button_notify_callback_t callback_{
      [](void* ctx, bool pressed) { return static_cast<FakeNotifyDevice*>(ctx)->notify_button_(); },
      this};
  bool getVal() { return val_; }
  void Wait() {
    sync_completion_wait(&event_, ZX_TIME_INFINITE);
    sync_completion_reset(&event_);
  }

 private:
  void notify_button_() {
    val_ = !val_;
    sync_completion_signal(&event_);
  }
  bool val_;

  sync_completion_t event_;
};

TEST(HidButtonsTest, Notify1) {
  // Hid Buttons Device
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Notify Device
  FakeNotifyDevice notify_dev(true);
  device.ButtonsRegisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev.callback_));

  // Interrupts
  EXPECT_EQ(notify_dev.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev.Wait();
  EXPECT_EQ(notify_dev.getVal(), false);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev.Wait();
  EXPECT_EQ(notify_dev.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev.Wait();
  EXPECT_EQ(notify_dev.getVal(), false);
  device.ButtonsUnregisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev.callback_));
  device.UnregisterTest();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, Notify2) {
  // Hid Buttons Device
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Notify Device 1
  FakeNotifyDevice notify_dev1(true);
  device.ButtonsRegisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev1.callback_));

  // Notify Device 2
  FakeNotifyDevice notify_dev2(true);
  device.ButtonsRegisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev2.callback_));

  // Interrupts
  EXPECT_EQ(notify_dev1.getVal(), true);
  EXPECT_EQ(notify_dev2.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev1.Wait();
  EXPECT_EQ(notify_dev1.getVal(), false);
  notify_dev2.Wait();
  EXPECT_EQ(notify_dev2.getVal(), false);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev1.Wait();
  EXPECT_EQ(notify_dev1.getVal(), true);
  notify_dev2.Wait();
  EXPECT_EQ(notify_dev2.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev1.Wait();
  EXPECT_EQ(notify_dev1.getVal(), false);
  notify_dev2.Wait();
  EXPECT_EQ(notify_dev2.getVal(), false);
  device.ButtonsUnregisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev1.callback_));
  device.ButtonsUnregisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev2.callback_));
  device.UnregisterTest();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, NotifyMultiple) {
  // Hid Buttons Device
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Notify Device 1
  FakeNotifyDevice notify_dev1(true);
  device.ButtonsRegisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev1.callback_));

  // Notify Device 2
  FakeNotifyDevice notify_dev2(true);
  device.ButtonsRegisterNotifyButton(BUTTON_TYPE_VOLUME_UP, &(notify_dev2.callback_));

  // Interrupts
  EXPECT_EQ(notify_dev1.getVal(), true);
  EXPECT_EQ(notify_dev2.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev1.Wait();
  EXPECT_EQ(notify_dev1.getVal(), false);
  EXPECT_EQ(notify_dev2.getVal(), true);
  device.FakeInterrupt(BUTTON_TYPE_VOLUME_UP);
  notify_dev2.Wait();
  EXPECT_EQ(notify_dev1.getVal(), false);
  EXPECT_EQ(notify_dev2.getVal(), false);
  device.ButtonsUnregisterNotifyButton(BUTTON_TYPE_VOLUME_UP, &(notify_dev2.callback_));
  device.FakeInterrupt(BUTTON_TYPE_MUTE);
  notify_dev1.Wait();
  EXPECT_EQ(notify_dev1.getVal(), true);
  EXPECT_EQ(notify_dev2.getVal(), false);
  device.ButtonsUnregisterNotifyButton(BUTTON_TYPE_MUTE, &(notify_dev1.callback_));
  device.UnregisterTest();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

}  // namespace buttons

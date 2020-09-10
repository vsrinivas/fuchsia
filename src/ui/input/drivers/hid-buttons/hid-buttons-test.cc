// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-buttons.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cstddef>

#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/protocol/buttons.h>
#include <ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "zircon/errors.h"

namespace {
static const buttons_button_config_t buttons_direct[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
};

static const buttons_gpio_config_t gpios_direct[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}}};

static const buttons_button_config_t buttons_multiple[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 1, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_CAM_MUTE, 2, 0, 0},
};

static const buttons_gpio_config_t gpios_multiple[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
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

static const buttons_button_config_t buttons_duplicate[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR, 2, 0, 0},
};

static const buttons_gpio_config_t gpios_duplicate[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
};
}  // namespace

namespace buttons {

enum class TestType {
  kTestDirect,
  kTestMatrix,
  kTestNotify,
  kTestDuplicate,
};

class HidButtonsDeviceTest : public HidButtonsDevice {
 public:
  HidButtonsDeviceTest(ddk::MockGpio* gpios, size_t gpios_count, TestType type)
      : HidButtonsDevice(fake_ddk::kFakeParent), type_(type) {
    fbl::AllocChecker ac;
    using MockPointer = ddk::MockGpio*;
    gpio_mocks_.reset(new (&ac) MockPointer[gpios_count], gpios_count);
    ZX_ASSERT(ac.check());
    for (size_t i = 0; i < gpios_count; ++i) {
      gpio_mocks_[i] = &gpios[i];
    }
  }

  void DdkUnbind(ddk::UnbindTxn txn) {
    // ShutDown() assigns nullptr to the function_ pointers.  Normally, the structures being pointed
    // at would be freed by the real DDK as a consequence of unbinding them.  However, in the test,
    // they need to be freed manually (necessitating a copy of the pointer).
    HidButtonsHidBusFunction* hidbus_function_copy_ = hidbus_function_;
    HidButtonsButtonsFunction* buttons_function_copy_ = buttons_function_;

    HidButtonsDevice::ShutDown();
    txn.Reply();

    delete hidbus_function_copy_;
    delete buttons_function_copy_;
  }
  void DdkRelease() { delete this; }

  void ShutDownTest() { DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice)); }

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
      case TestType::kTestDuplicate:
        mock->ExpectConfigIn(ZX_OK, gpios_duplicate[gpio_index].internal_pull)
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
          gpio_mocks_[buttons[i].gpioA_idx]->ExpectRead(ZX_OK, 0);
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
          gpio_mocks_[buttons[i].gpioB_idx]->ExpectConfigIn(ZX_OK, 0x2);
          gpio_mocks_[buttons[i].gpioA_idx]->ExpectRead(ZX_OK, 0);
          gpio_mocks_[buttons[i].gpioB_idx]->ExpectConfigOut(
              ZX_OK, gpios[buttons[i].gpioB_idx].config.output_value);
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
          gpio_mocks_[buttons[i].gpioA_idx]->ExpectRead(ZX_OK, 0);
        }
        return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
      }
      case TestType::kTestDuplicate: {
        for (size_t i = 0; i < n_gpios; ++i) {
          gpios[i].config = gpios_duplicate[i];
        }
        constexpr size_t n_buttons = countof(buttons_duplicate);
        auto buttons = fbl::Array(new buttons_button_config_t[n_buttons], n_buttons);
        for (size_t i = 0; i < n_buttons; ++i) {
          buttons[i] = buttons_duplicate[i];
          gpio_mocks_[buttons[i].gpioA_idx]->ExpectRead(ZX_OK, 0);
        }
        return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
      }
    }
    return ZX_OK;
  }

  void FakeInterrupt() {
    // Issue the first interrupt.
    zx_port_packet packet = {kPortKeyInterruptStart + 0, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

  void FakeInterrupt(ButtonType type) {
    // Issue the first interrupt.
    zx_port_packet packet = {kPortKeyInterruptStart + button_map_[static_cast<uint8_t>(type)],
                             ZX_PKT_TYPE_USER,
                             ZX_OK,
                             {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

  void DebounceWait() {
    sync_completion_wait(&debounce_threshold_passed_, ZX_TIME_INFINITE);
    sync_completion_reset(&debounce_threshold_passed_);
  }

  void ClosingChannel(uint64_t id) override {
    HidButtonsDevice::ClosingChannel(id);
    sync_completion_signal(&test_channels_cleared_);
  }

  void Notify(uint32_t type) override {
    HidButtonsDevice::Notify(type);
    sync_completion_signal(&debounce_threshold_passed_);
  }

  void Wait() {
    sync_completion_wait(&test_channels_cleared_, ZX_TIME_INFINITE);
    sync_completion_reset(&test_channels_cleared_);
  }

  HidButtonsButtonsFunction* GetButtonsFn() { return GetButtonsFunction(); }

 private:
  sync_completion_t test_channels_cleared_;
  sync_completion_t debounce_threshold_passed_;

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

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonUnpushedReport) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 0;  // Unpushed.
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushedReport) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 1;  // Pushed
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonPushUnpushPush) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  EXPECT_OK(device.BindTest());

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
}

TEST(HidButtonsTest, DirectButtonFlaky) {
  ddk::MockGpio mock_gpios[1];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDirect);
  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
  device.SetupGpio(std::move(irq), 0);

  EXPECT_OK(device.BindTest());

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
  device.FakeInterrupt();
  device.DebounceWait();

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

  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.volume_up = 1;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);

  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_matrix); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, ButtonsProtocolTest) {
  // Hid Buttons Device
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  zx::channel client_end, server_end;
  zx::channel::create(0, &client_end, &server_end);
  device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end));
  client_end.reset();
  device.Wait();

  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, GetStateTest) {
  // Hid Buttons Device
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  {  // Scoping for Client
    zx::channel client_end, server_end;
    EXPECT_OK(zx::channel::create(0, &client_end, &server_end));
    device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end));
    Buttons::SyncClient client(std::move(client_end));

    // Reconfigure Polarity due to interrupt.
    mock_gpios[1].ExpectRead(ZX_OK, 1);  // Read value.

    auto result = client.GetState(ButtonType::MUTE);
    EXPECT_EQ(result->pressed, true);
  }  // Close Client

  device.Wait();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

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
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  {  // Scoping for Client
    zx::channel client_end, server_end;
    EXPECT_OK(zx::channel::create(0, &client_end, &server_end));
    device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end));
    Buttons::SyncClient client(std::move(client_end));
    auto result1 = client.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::MUTE));
    EXPECT_OK(result1.status());

    // Interrupts
    device.FakeInterrupt(ButtonType::MUTE);
    device.DebounceWait();
    {
      Buttons::EventHandlers event_handlers{
          .on_notify =
              [](Buttons::OnNotifyResponse* message) {
                if (message->type == ButtonType::MUTE && message->pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      };
      EXPECT_TRUE(client.HandleEvents(event_handlers).ok());
    }
    device.FakeInterrupt(ButtonType::MUTE);
    device.DebounceWait();
    {
      Buttons::EventHandlers event_handlers{
          .on_notify =
              [](Buttons::OnNotifyResponse* message) {
                if (message->type == ButtonType::MUTE && message->pressed == false) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      };
      EXPECT_TRUE(client.HandleEvents(event_handlers).ok());
    }
    auto result2 = client.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::VOLUME_UP));
    EXPECT_OK(result2.status());
    device.FakeInterrupt(ButtonType::VOLUME_UP);
    device.DebounceWait();
    {
      Buttons::EventHandlers event_handlers{
          .on_notify =
              [](Buttons::OnNotifyResponse* message) {
                if (message->type == ButtonType::VOLUME_UP && message->pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      };
      EXPECT_TRUE(client.HandleEvents(event_handlers).ok());
    }
  }  // Close Client

  device.Wait();
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
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  {  // Scoping for Client 2
    // Client 2
    zx::channel client_end2, server_end2;
    EXPECT_OK(zx::channel::create(0, &client_end2, &server_end2));
    device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end2));
    Buttons::SyncClient client2(std::move(client_end2));
    auto result2_1 = client2.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::MUTE));
    EXPECT_OK(result2_1.status());

    {  // Scoping for Client 1
      // Client 1
      zx::channel client_end1, server_end1;
      EXPECT_OK(zx::channel::create(0, &client_end1, &server_end1));
      device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end1));
      Buttons::SyncClient client1(std::move(client_end1));
      auto result1_1 = client1.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::MUTE));
      EXPECT_OK(result1_1.status());

      // Interrupts
      device.FakeInterrupt(ButtonType::MUTE);
      device.DebounceWait();
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::MUTE && message->pressed == true) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client1.HandleEvents(event_handlers).ok());
      }
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::MUTE && message->pressed == true) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client2.HandleEvents(event_handlers).ok());
      }
      device.FakeInterrupt(ButtonType::MUTE);
      device.DebounceWait();
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::MUTE && message->pressed == false) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client1.HandleEvents(event_handlers).ok());
      }
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::MUTE && message->pressed == false) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client2.HandleEvents(event_handlers).ok());
      }
      auto result1_2 = client1.RegisterNotify((1 << static_cast<uint8_t>(ButtonType::VOLUME_UP)) |
                                              (1 << static_cast<uint8_t>(ButtonType::MUTE)));
      EXPECT_OK(result1_2.status());
      auto result2_2 = client2.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::VOLUME_UP));
      EXPECT_OK(result2_2.status());
      device.FakeInterrupt(ButtonType::MUTE);
      device.DebounceWait();
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::MUTE && message->pressed == true) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client1.HandleEvents(event_handlers).ok());
      }
      device.FakeInterrupt(ButtonType::VOLUME_UP);
      device.DebounceWait();
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::VOLUME_UP && message->pressed == false) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client1.HandleEvents(event_handlers).ok());
      }
      {
        Buttons::EventHandlers event_handlers{
            .on_notify =
                [](Buttons::OnNotifyResponse* message) {
                  if (message->type == ButtonType::VOLUME_UP && message->pressed == false) {
                    return ZX_OK;
                  }
                  return ZX_ERR_INTERNAL;
                },
            .unknown = [] { return ZX_ERR_INVALID_ARGS; },
        };
        EXPECT_TRUE(client2.HandleEvents(event_handlers).ok());
      }
    }  // Close Client 1

    device.Wait();
    device.FakeInterrupt(ButtonType::VOLUME_UP);
    device.DebounceWait();
    {
      Buttons::EventHandlers event_handlers{
          .on_notify =
              [](Buttons::OnNotifyResponse* message) {
                if (message->type == ButtonType::VOLUME_UP && message->pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      };
      EXPECT_TRUE(client2.HandleEvents(event_handlers).ok());
    }
  }  // Close Client 2

  device.Wait();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, DuplicateReports) {
  ddk::MockGpio mock_gpios[3];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestDuplicate);
  zx::interrupt irqs[3];
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[0]);
  device.SetupGpio(std::move(irqs[0]), 0);
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[1]);
  device.SetupGpio(std::move(irqs[1]), 1);
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[2]);
  device.SetupGpio(std::move(irqs[2]), 2);

  EXPECT_OK(device.BindTest());

  // Holding FDR (VOL_UP and VOL_DOWN), then release VOL_UP, should only get one report.
  // Reconfigure Polarity due to interrupt.
  mock_gpios[2]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt(ButtonType::RESET);
  device.DebounceWait();

  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  device.FakeInterrupt(ButtonType::VOLUME_UP);
  device.DebounceWait();

  mock_gpios[2]
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  device.FakeInterrupt(ButtonType::RESET);
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t reports[2];
    reports[0] = {};
    reports[0].rpt_id = 1;
    reports[0].volume_up = 1;    // Pushed.
    reports[0].volume_down = 1;  // Pushed.
    reports[0].reset = 1;        // Pushed.
    reports[1] = {};
    reports[1].rpt_id = 1;
    reports[1].volume_up = 0;    // Unpushed.
    reports[1].volume_down = 1;  // Pushed.
    reports[1].reset = 0;        // Unpushed.
    ASSERT_BYTES_EQ(buffer, reports, size);
    EXPECT_EQ(size, sizeof(reports));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  device.HidbusStart(&protocol);

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURES(mock_gpios[0].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_gpios[1].VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(mock_gpios[2].VerifyAndClear());
}

TEST(HidButtonsTest, CamMute) {
  ddk::MockGpio mock_gpios[countof(gpios_multiple)];
  HidButtonsDeviceTest device(mock_gpios, countof(mock_gpios), TestType::kTestNotify);
  zx::interrupt irqs[countof(gpios_multiple)];
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irqs[i]);
    device.SetupGpio(std::move(irqs[i]), i);
  }

  EXPECT_OK(device.BindTest());

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.camera_access_disabled = 1;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  EXPECT_OK(device.HidbusStart(&protocol));

  mock_gpios[2]
      .ExpectRead(ZX_OK, 1)                         // On.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still on, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);               // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  device.FakeInterrupt(ButtonType::CAM_MUTE);
  device.DebounceWait();

  device.HidbusStop();

  ops.io_queue = [](void* ctx, const void* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.camera_access_disabled = 0;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  protocol.ops = &ops;
  EXPECT_OK(device.HidbusStart(&protocol));

  mock_gpios[2]
      .ExpectRead(ZX_OK, 0)                          // Off.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still off, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[2].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  device.FakeInterrupt(ButtonType::CAM_MUTE);
  device.DebounceWait();

  device.ShutDownTest();
  for (auto& mock_gpio : mock_gpios) {
    mock_gpio.VerifyAndClear();
  }
}

}  // namespace buttons

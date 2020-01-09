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
    ZX_ASSERT(ac.check());
    for (size_t i = 0; i < gpios_count; ++i) {
      gpio_mocks_[i] = &gpios[i];
    }
  }

  void DdkUnbindDeprecated() {
    HidButtonsDevice::ShutDown();
    DdkRemoveDeprecated();
  }
  void DdkRelease() { delete this; }

  void ShutDownTest() { DdkUnbindDeprecated(); }

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

  // Reconfigure Polarity due to interrupt.
  mock_gpios[1]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

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
    EXPECT_OK(client.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::MUTE && pressed == true) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
    device.FakeInterrupt(ButtonType::MUTE);
    device.DebounceWait();
    EXPECT_OK(client.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::MUTE && pressed == false) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
    auto result2 = client.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::VOLUME_UP));
    EXPECT_OK(result2.status());
    device.FakeInterrupt(ButtonType::VOLUME_UP);
    device.DebounceWait();
    EXPECT_OK(client.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::VOLUME_UP && pressed == true) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
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

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 0);                // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 0);                // Read value to prepare report.

  // Reconfigure Polarity due to interrupt.
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

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
      EXPECT_OK(client1.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::MUTE && pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      EXPECT_OK(client2.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::MUTE && pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      device.FakeInterrupt(ButtonType::MUTE);
      device.DebounceWait();
      EXPECT_OK(client1.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::MUTE && pressed == false) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      EXPECT_OK(client2.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::MUTE && pressed == false) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      auto result1_2 = client1.RegisterNotify((1 << static_cast<uint8_t>(ButtonType::VOLUME_UP)) |
                                              (1 << static_cast<uint8_t>(ButtonType::MUTE)));
      EXPECT_OK(result1_2.status());
      auto result2_2 = client2.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::VOLUME_UP));
      EXPECT_OK(result2_2.status());
      device.FakeInterrupt(ButtonType::MUTE);
      device.DebounceWait();
      EXPECT_OK(client1.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::MUTE && pressed == true) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      device.FakeInterrupt(ButtonType::VOLUME_UP);
      device.DebounceWait();
      EXPECT_OK(client1.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::VOLUME_UP && pressed == false) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
      EXPECT_OK(client2.HandleEvents(Buttons::EventHandlers{
          .on_notify =
              [](ButtonType type, bool pressed) {
                if (type == ButtonType::VOLUME_UP && pressed == false) {
                  return ZX_OK;
                }
                return ZX_ERR_INTERNAL;
              },
          .unknown = [] { return ZX_ERR_INVALID_ARGS; },
      }));
    }  // Close Client 1

    device.Wait();
    device.FakeInterrupt(ButtonType::VOLUME_UP);
    device.DebounceWait();
    EXPECT_OK(client2.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::VOLUME_UP && pressed == true) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
  }  // Close Client 2

  device.Wait();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}

TEST(HidButtonsTest, DebounceTest) {
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
  mock_gpios[0]
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  mock_gpios[0].ExpectRead(ZX_OK, 1);               // Read value to prepare report.
  mock_gpios[1].ExpectRead(ZX_OK, 1);               // Read value to prepare report.

  {  // Scoping for Client
    zx::channel client_end, server_end;
    EXPECT_OK(zx::channel::create(0, &client_end, &server_end));
    device.GetButtonsFn()->ButtonsGetChannel(std::move(server_end));
    Buttons::SyncClient client(std::move(client_end));
    auto result1 = client.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::MUTE));
    EXPECT_OK(result1.status());

    // Interrupts
    device.FakeInterrupt(ButtonType::MUTE);
    device.FakeInterrupt(ButtonType::MUTE);  // Bounce (within debounce threshold).
    device.DebounceWait();
    EXPECT_OK(client.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::MUTE && pressed == false) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
    auto result2 = client.RegisterNotify(1 << static_cast<uint8_t>(ButtonType::VOLUME_UP));
    EXPECT_OK(result2.status());
    device.FakeInterrupt(ButtonType::VOLUME_UP);
    device.DebounceWait();
    EXPECT_OK(client.HandleEvents(Buttons::EventHandlers{
        .on_notify =
            [](ButtonType type, bool pressed) {
              if (type == ButtonType::VOLUME_UP && pressed == true) {
                return ZX_OK;
              }
              return ZX_ERR_INTERNAL;
            },
        .unknown = [] { return ZX_ERR_INVALID_ARGS; },
    }));
  }  // Close Client

  device.Wait();
  device.ShutDownTest();
  for (size_t i = 0; i < countof(gpios_multiple); ++i) {
    ASSERT_NO_FATAL_FAILURES(mock_gpios[i].VerifyAndClear());
  }
}
}  // namespace buttons

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-buttons.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/ddk/metadata.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cstddef>

#include <ddk/metadata/buttons.h>
#include <zxtest/zxtest.h>

#include "zircon/errors.h"

namespace {
static const buttons_button_config_t buttons_direct[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
};

static const buttons_gpio_config_t gpios_direct[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}}};

static const buttons_button_config_t buttons_multiple[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 1, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_CAM_MUTE, 2, 0, 0},
};

static const buttons_gpio_config_t gpios_multiple[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
};

static const buttons_gpio_config_t gpios_multiple_one_polled[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
    {BUTTONS_GPIO_TYPE_POLL, 0, {.poll = {GPIO_NO_PULL, zx::msec(20).get()}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
};

static const buttons_button_config_t buttons_matrix[] = {
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP, 0, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A, 1, 2, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M, 0, 3, 0},
    {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
};

static const buttons_gpio_config_t gpios_matrix[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_PULL_UP}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_PULL_UP}}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {.matrix = {0}}},
    {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, 0, {.matrix = {0}}},
};

static const buttons_button_config_t buttons_duplicate[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR, 2, 0, 0},
};

static const buttons_gpio_config_t gpios_duplicate[] = {
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
    {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
};
}  // namespace

namespace buttons {

class MockGpio : public ddk::MockGpio {
 public:
  MockGpio& SetDefaultReadValue(uint8_t value) {
    default_read_value_ = value;
    use_default_read_value_ = true;
    return *this;
  }

  zx_status_t GpioRead(uint8_t* out_value) override {
    if (use_default_read_value_) {
      *out_value = default_read_value_;
      return ZX_OK;
    }
    return ddk::MockGpio::GpioRead(out_value);
  }

  void VerifyAndClear() {
    mock_config_in_.VerifyAndClear();
    mock_config_out_.VerifyAndClear();
    mock_set_alt_function_.VerifyAndClear();
    mock_write_.VerifyAndClear();
    mock_get_interrupt_.VerifyAndClear();
    mock_release_interrupt_.VerifyAndClear();
    mock_set_polarity_.VerifyAndClear();
    mock_set_drive_strength_.VerifyAndClear();

    if (!use_default_read_value_) {
      mock_read_.VerifyAndClear();
    }
  }

 private:
  bool use_default_read_value_ = false;
  uint8_t default_read_value_ = 0;
};

class HidButtonsDeviceTest : public HidButtonsDevice {
 public:
  HidButtonsDeviceTest() : HidButtonsDevice(fake_ddk::kFakeParent) {}

  void DdkUnbind(ddk::UnbindTxn txn) {
    // ShutDown() assigns nullptr to the function_ pointers.  Normally, the structures being pointed
    // at would be freed by the real DDK as a consequence of unbinding them.  However, in the test,
    // they need to be freed manually (necessitating a copy of the pointer).
    HidButtonsHidBusFunction* hidbus_function_copy_ = hidbus_function_;

    HidButtonsDevice::ShutDown();
    txn.Reply();

    delete hidbus_function_copy_;
  }
  void DdkRelease() { delete this; }

  void ShutDownTest() { DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice)); }

  MockGpio& GetGpio(size_t index) { return gpio_mocks_[index]; }

  void VerifyAndClearGpios() {
    for (auto& gpio : gpio_mocks_) {
      ASSERT_NO_FATAL_FAILURE(gpio.VerifyAndClear());
    }
  }

  void SetupGpio(MockGpio* mock, const buttons_gpio_config_t& gpio_config, zx::interrupt irq) {
    mock->ExpectSetAltFunction(ZX_OK, 0);

    if (gpio_config.type == BUTTONS_GPIO_TYPE_INTERRUPT) {
      mock->ExpectConfigIn(ZX_OK, gpio_config.interrupt.internal_pull)
          .ExpectRead(ZX_OK, 0)  // Not pushed, low.
          .ExpectReleaseInterrupt(ZX_OK)
          .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_HIGH, std::move(irq));

      // Make sure polarity is correct in case it changed during configuration.
      mock->ExpectRead(ZX_OK, 0)                         // Not pushed.
          .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Set correct polarity.
          .ExpectRead(ZX_OK, 0);                         // Still not pushed.
    } else if (gpio_config.type == BUTTONS_GPIO_TYPE_MATRIX_OUTPUT) {
      mock->ExpectConfigOut(ZX_OK, gpio_config.matrix.output_value);
    } else if (gpio_config.type == BUTTONS_GPIO_TYPE_POLL) {
      mock->ExpectConfigIn(ZX_OK, gpio_config.poll.internal_pull);
      mock->SetDefaultReadValue(0);
    }
  }

  zx_status_t BindTest(const buttons_gpio_config_t* gpios_config, size_t gpios_config_size,
                       const buttons_button_config_t* buttons_config, size_t buttons_config_size) {
    gpio_mocks_ = std::vector<MockGpio>(gpios_config_size);
    for (size_t i = 0; i < gpio_mocks_.size(); i++) {
      zx::interrupt irq;
      zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);
      SetupGpio(&gpio_mocks_[i], gpios_config[i], std::move(irq));
    }

    const size_t n_gpios = gpios_config_size;
    auto gpios = fbl::Array(new HidButtonsDevice::Gpio[n_gpios], n_gpios);
    const size_t n_buttons = buttons_config_size;
    auto buttons = fbl::Array(new buttons_button_config_t[n_buttons], n_buttons);
    for (size_t i = 0; i < n_gpios; ++i) {
      gpios[i].gpio = *gpio_mocks_[i].GetProto();
      gpios[i].config = gpios_config[i];
    }

    for (size_t i = 0; i < n_buttons; ++i) {
      buttons[i] = buttons_config[i];
      switch (buttons_config[i].type) {
        case BUTTONS_TYPE_DIRECT: {
          gpio_mocks_[buttons[i].gpioA_idx].ExpectRead(ZX_OK, 0);
          break;
        }
        case BUTTONS_TYPE_MATRIX: {
          gpio_mocks_[buttons[i].gpioB_idx].ExpectConfigIn(ZX_OK, 0x2);
          gpio_mocks_[buttons[i].gpioA_idx].ExpectRead(ZX_OK, 0);
          gpio_mocks_[buttons[i].gpioB_idx].ExpectConfigOut(
              ZX_OK, gpios[buttons[i].gpioB_idx].config.matrix.output_value);
          break;
        }
        default:
          return ZX_ERR_INTERNAL;
      }
    }

    return HidButtonsDevice::Bind(std::move(gpios), std::move(buttons));
  }

  void FakeInterrupt() {
    // Issue the first interrupt.
    zx_port_packet packet = {kPortKeyInterruptStart + 0, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

  void FakeInterrupt(ButtonType type) {
    // Issue the first interrupt.
    zx_port_packet packet = {
        kPortKeyInterruptStart + button_map_[type], ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
  }

  void DebounceWait() {
    sync_completion_wait(&debounce_threshold_passed_, ZX_TIME_INFINITE);
    sync_completion_reset(&debounce_threshold_passed_);
  }

  void ClosingChannel(ButtonsNotifyInterface* interface) override {
    HidButtonsDevice::ClosingChannel(interface);
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

 private:
  sync_completion_t test_channels_cleared_;
  sync_completion_t debounce_threshold_passed_;

  std::vector<MockGpio> gpio_mocks_;
};

TEST(HidButtonsTest, DirectButtonBind) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DirectButtonPush) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                         // Still pushed, ok to continue.
      .ExpectRead(ZX_OK, 1);                        // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DirectButtonUnpushedReport) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0)                          // Still not pushed, ok to continue.
      .ExpectRead(ZX_OK, 0);                         // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
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
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DirectButtonPushedReport) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                         // Still pushed, ok to continue.
      .ExpectRead(ZX_OK, 1);                        // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
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
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DirectButtonPushUnpushPush) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                         // Still pushed, ok to continue.
      .ExpectRead(ZX_OK, 1);                        // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 0)                          // Not pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0)                          // Still not pushed, ok to continue.
      .ExpectRead(ZX_OK, 0);                         // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1)                         // Still pushed, ok to continue.
      .ExpectRead(ZX_OK, 1);                        // Read value to prepare report.
  device.FakeInterrupt();
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DirectButtonFlaky) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_direct, std::size(gpios_direct), buttons_direct,
                            std::size(buttons_direct)));

  // Reconfigure Polarity due to interrupt and keep checking until correct.
  device.GetGpio(0)
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
  device.GetGpio(0).ExpectRead(ZX_OK, 1);  // Pushed.
  device.FakeInterrupt();
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, MatrixButtonBind) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_matrix, std::size(gpios_matrix), buttons_matrix,
                            std::size(buttons_matrix)));

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, MatrixButtonPush) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_matrix, std::size(gpios_matrix), buttons_matrix,
                            std::size(buttons_matrix)));

  // Reconfigure Polarity due to interrupt.
  device.GetGpio(0)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.

  // Matrix Scan for 0.
  device.GetGpio(2).ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                          // Float column.
  device.GetGpio(0).ExpectRead(ZX_OK, 1);                                         // Read row.
  device.GetGpio(2).ExpectConfigOut(ZX_OK, gpios_matrix[2].matrix.output_value);  // Restore column.

  // Matrix Scan for 1.
  device.GetGpio(2).ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                          // Float column.
  device.GetGpio(1).ExpectRead(ZX_OK, 0);                                         // Read row.
  device.GetGpio(2).ExpectConfigOut(ZX_OK, gpios_matrix[2].matrix.output_value);  // Restore column.

  // Matrix Scan for 2.
  device.GetGpio(3).ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                          // Float column.
  device.GetGpio(0).ExpectRead(ZX_OK, 0);                                         // Read row.
  device.GetGpio(3).ExpectConfigOut(ZX_OK, gpios_matrix[3].matrix.output_value);  // Restore column.

  // Matrix Scan for 3.
  device.GetGpio(3).ExpectConfigIn(ZX_OK, GPIO_NO_PULL);                          // Float column.
  device.GetGpio(1).ExpectRead(ZX_OK, 0);                                         // Read row.
  device.GetGpio(3).ExpectConfigOut(ZX_OK, gpios_matrix[3].matrix.output_value);  // Restore column.

  device.FakeInterrupt();
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
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
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, DuplicateReports) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_duplicate, std::size(gpios_duplicate), buttons_duplicate,
                            std::size(buttons_duplicate)));

  // Holding FDR (VOL_UP and VOL_DOWN), then release VOL_UP, should only get one report.
  // Reconfigure Polarity due to interrupt.
  device.GetGpio(2)
      .ExpectRead(ZX_OK, 1)                         // Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still pushed, ok to continue.
  device.GetGpio(0).ExpectRead(ZX_OK, 1);           // Read value to prepare report.
  device.GetGpio(1).ExpectRead(ZX_OK, 1);           // Read value to prepare report.
  device.GetGpio(2).ExpectRead(ZX_OK, 1);           // Read value to prepare report.
  device.FakeInterrupt(ButtonType::kReset);
  device.DebounceWait();

  device.GetGpio(0)
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  device.GetGpio(0).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.GetGpio(1).ExpectRead(ZX_OK, 1);            // Read value to prepare report.
  device.GetGpio(2).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.FakeInterrupt(ButtonType::kVolumeUp);
  device.DebounceWait();

  device.GetGpio(2)
      .ExpectRead(ZX_OK, 0)                          // Not Pushed.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Keep the correct polarity.
      .ExpectRead(ZX_OK, 0);                         // Still not pushed, ok to continue.
  device.GetGpio(0).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.GetGpio(1).ExpectRead(ZX_OK, 1);            // Read value to prepare report.
  device.GetGpio(2).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.FakeInterrupt(ButtonType::kReset);
  device.DebounceWait();

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
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
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, CamMute) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_multiple, std::size(gpios_multiple), buttons_multiple,
                            std::size(buttons_multiple)));

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.camera_access_disabled = 1;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  hidbus_ifc_protocol_t protocol = {};
  protocol.ops = &ops;
  EXPECT_OK(device.HidbusStart(&protocol));

  device.GetGpio(2)
      .ExpectRead(ZX_OK, 1)                         // On.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW)  // Turn the polarity.
      .ExpectRead(ZX_OK, 1);                        // Still on, ok to continue.
  device.GetGpio(0).ExpectRead(ZX_OK, 0);           // Read value to prepare report.
  device.GetGpio(1).ExpectRead(ZX_OK, 0);           // Read value to prepare report.
  device.GetGpio(2).ExpectRead(ZX_OK, 1);           // Read value to prepare report.
  device.FakeInterrupt(ButtonType::kCamMute);
  device.DebounceWait();

  device.HidbusStop();

  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report_volume_up = {};
    report_volume_up.rpt_id = 1;
    report_volume_up.camera_access_disabled = 0;
    ASSERT_BYTES_EQ(buffer, &report_volume_up, size);
    EXPECT_EQ(size, sizeof(report_volume_up));
  };
  protocol.ops = &ops;
  EXPECT_OK(device.HidbusStart(&protocol));

  device.GetGpio(2)
      .ExpectRead(ZX_OK, 0)                          // Off.
      .ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH)  // Turn the polarity.
      .ExpectRead(ZX_OK, 0);                         // Still off, ok to continue.
  device.GetGpio(0).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.GetGpio(1).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.GetGpio(2).ExpectRead(ZX_OK, 0);            // Read value to prepare report.
  device.FakeInterrupt(ButtonType::kCamMute);
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());
}

TEST(HidButtonsTest, PollOneButton) {
  HidButtonsDeviceTest device;
  EXPECT_OK(device.BindTest(gpios_multiple_one_polled, std::size(gpios_multiple_one_polled),
                            buttons_multiple, std::size(buttons_multiple)));

  // All GPIOs must have a default read value if polling is being used, as they are all ready
  // every poll period.
  device.GetGpio(2).SetDefaultReadValue(0);

  std::vector<buttons_input_rpt_t> reports;

  hidbus_ifc_protocol_ops_t ops = {};
  ops.io_queue = [](void* ctx, const uint8_t* buffer, size_t size, zx_time_t time) {
    buttons_input_rpt_t report;
    ASSERT_EQ(size, sizeof(report));
    memcpy(&report, buffer, size);
    reinterpret_cast<std::vector<buttons_input_rpt_t>*>(ctx)->push_back(report);
  };
  hidbus_ifc_protocol_t protocol = {.ops = &ops, .ctx = &reports};
  device.HidbusStart(&protocol);

  device.GetGpio(0).SetDefaultReadValue(1).ExpectSetPolarity(ZX_OK, GPIO_POLARITY_LOW);
  device.FakeInterrupt();
  device.DebounceWait();

  device.GetGpio(1).SetDefaultReadValue(1);
  device.DebounceWait();

  device.GetGpio(0).SetDefaultReadValue(0).ExpectSetPolarity(ZX_OK, GPIO_POLARITY_HIGH);
  device.FakeInterrupt();
  device.DebounceWait();

  device.GetGpio(1).SetDefaultReadValue(0);
  device.DebounceWait();

  device.ShutDownTest();
  ASSERT_NO_FATAL_FAILURE(device.VerifyAndClearGpios());

  ASSERT_EQ(reports.size(), 4);

  EXPECT_EQ(reports[0].rpt_id, BUTTONS_RPT_ID_INPUT);
  EXPECT_EQ(reports[0].volume_up, 1);
  EXPECT_EQ(reports[0].mute, 0);

  EXPECT_EQ(reports[1].rpt_id, BUTTONS_RPT_ID_INPUT);
  EXPECT_EQ(reports[1].volume_up, 1);
  EXPECT_EQ(reports[1].mute, 1);

  EXPECT_EQ(reports[2].rpt_id, BUTTONS_RPT_ID_INPUT);
  EXPECT_EQ(reports[2].volume_up, 0);
  EXPECT_EQ(reports[2].mute, 1);

  EXPECT_EQ(reports[3].rpt_id, BUTTONS_RPT_ID_INPUT);
  EXPECT_EQ(reports[3].volume_up, 0);
  EXPECT_EQ(reports[3].mute, 0);
}

}  // namespace buttons

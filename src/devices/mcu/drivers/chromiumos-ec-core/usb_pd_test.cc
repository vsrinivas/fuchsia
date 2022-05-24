// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/usb_pd.h"

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace chromiumos_ec_core::usb_pd {

class ChromiumosEcUsbPdTest : public ChromiumosEcTestBase {
 public:
  void SetUp() override {
    ChromiumosEcTestBase::SetUp();
    ASSERT_OK(loop_.StartThread("cros-ec-usb-pd-test-fidl"));

    fake_ec_.SetFeatures({EC_FEATURE_USB_PD});

    fake_ec_.AddCommand(EC_CMD_USB_PD_PORTS, 0,
                        [this](const void* data, size_t data_size, auto& completer) {
                          IssueCommand(EC_CMD_USB_PD_PORTS, data, data_size, completer);
                        });
    fake_ec_.AddCommand(EC_CMD_USB_PD_POWER_INFO, 0,
                        [this](const void* data, size_t data_size, auto& completer) {
                          IssueCommand(EC_CMD_USB_PD_POWER_INFO, data, data_size, completer);
                        });

    // Calls DdkInit on the cros-ec-core device.
    ASSERT_NO_FATAL_FAILURE(InitDevice());

    // Initialise the usbpd device.
    zx_device* usbpd_dev = ChromiumosEcTestBase::device_->zxdev()->GetLatestChild();
    usbpd_dev->InitOp();
    ASSERT_OK(usbpd_dev->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = usbpd_dev->GetDeviceContext<AcpiCrOsEcUsbPdDevice>();

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_power::Source>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), device_);
    client_.Bind(std::move(endpoints->client));
  }

  void IssueCommand(uint16_t command, const void* input, size_t input_size,
                    FakeEcDevice::RunCommandCompleter::Sync& completer) {
    using fuchsia_hardware_google_ec::wire::EcStatus;
    switch (command) {
      case EC_CMD_USB_PD_PORTS: {
        ec_response_usb_pd_ports response{
            .num_ports = 1,
        };
        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
        break;
      }

      case EC_CMD_USB_PD_POWER_INFO: {
        if (input_size < sizeof(ec_params_usb_pd_power_info)) {
          completer.ReplyError(ZX_ERR_BUFFER_TOO_SMALL);
          return;
        }
        auto request = reinterpret_cast<const ec_params_usb_pd_power_info*>(input);

        if (request->port != 0) {
          completer.ReplyError(ZX_ERR_IO);
          return;
        }

        ec_response_usb_pd_power_info response{
            .role = static_cast<uint8_t>(role_),
            .type = 0,
            .dualrole = 0,
            .reserved1 = 0,
            .meas =
                {
                    .voltage_max = 0,
                    .voltage_now = 0,
                    .current_max = 0,
                    .current_lim = 0,
                },
            .max_power = 0,
        };

        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
        break;
      }

      default:
        completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    }
  }

  void SetChargeState(bool charging) {
    role_ = charging ? USB_PD_PORT_POWER_SINK : USB_PD_PORT_POWER_SINK_NOT_CHARGING;
  }

 protected:
  usb_power_roles role_ = USB_PD_PORT_POWER_SINK_NOT_CHARGING;
  AcpiCrOsEcUsbPdDevice* device_;
  fidl::WireSyncClient<fuchsia_hardware_power::Source> client_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

TEST_F(ChromiumosEcUsbPdTest, PowerInfo) {
  auto result = client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value_NEW().status, ZX_OK);
  EXPECT_EQ(result.value_NEW().info.type, fuchsia_hardware_power::wire::PowerType::kAc);
  EXPECT_EQ(result.value_NEW().info.state, fuchsia_hardware_power::wire::kPowerStateDischarging);
}

TEST_F(ChromiumosEcUsbPdTest, PowerInfoCharging) {
  SetChargeState(true);

  auto result = client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value_NEW().status, ZX_OK);
  EXPECT_EQ(result.value_NEW().info.type, fuchsia_hardware_power::wire::PowerType::kAc);
  EXPECT_EQ(result.value_NEW().info.state, fuchsia_hardware_power::wire::kPowerStateCharging);
}

TEST_F(ChromiumosEcUsbPdTest, BatteryInfo) {
  auto result = client_->GetBatteryInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value_NEW().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(ChromiumosEcUsbPdTest, StateChangeEvent) {
  auto state_change_result = client_->GetStateChangeEvent();
  ASSERT_TRUE(state_change_result.ok());
  EXPECT_EQ(state_change_result.value_NEW().status, ZX_OK);

  zx_signals_t signals;
  zx::event event(std::move(state_change_result.value_NEW().handle));
  event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(0)), &signals);
  EXPECT_EQ(signals, 0);

  SetChargeState(true);
  device_->NotifyHandler(0x80);

  event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &signals);
  EXPECT_EQ(signals, ZX_USER_SIGNAL_0);

  auto result = client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value_NEW().status, ZX_OK);
  EXPECT_EQ(result.value_NEW().info.state, fuchsia_hardware_power::wire::kPowerStateCharging);

  // Signal is cleared by call to GetPowerInfo.
  event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(0)), &signals);
  EXPECT_EQ(signals, 0);
}
}  // namespace chromiumos_ec_core::usb_pd

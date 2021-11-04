// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-pd.h"

#include <inttypes.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "dev.h"

class FakeUsbPdEc : public cros_ec::EmbeddedController {
 public:
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* input,
                           size_t input_size, void* result, size_t result_buff_size,
                           size_t* actual) override {
    switch (command) {
      case EC_CMD_USB_PD_PORTS: {
        if (result_buff_size < sizeof(ec_response_usb_pd_ports)) {
          return ZX_ERR_BUFFER_TOO_SMALL;
        }
        auto response = reinterpret_cast<ec_response_usb_pd_ports*>(result);
        response->num_ports = 1;
        *actual = sizeof(ec_response_usb_pd_ports);
        break;
      }

      case EC_CMD_USB_PD_POWER_INFO: {
        if (input_size < sizeof(ec_params_usb_pd_power_info)) {
          return ZX_ERR_BUFFER_TOO_SMALL;
        }
        auto request = reinterpret_cast<const ec_params_usb_pd_power_info*>(input);

        if (request->port != 0) {
          return ZX_ERR_IO;
        }

        if (result_buff_size < sizeof(ec_response_usb_pd_power_info)) {
          return ZX_ERR_BUFFER_TOO_SMALL;
        }
        auto response = reinterpret_cast<ec_response_usb_pd_power_info*>(result);

        response->role = role_;
        response->type = 0;
        response->dualrole = 0;
        response->reserved1 = 0;
        response->meas.voltage_max = 0;
        response->meas.voltage_now = 0;
        response->meas.current_max = 0;
        response->meas.current_lim = 0;
        response->max_power = 0;

        *actual = sizeof(ec_response_usb_pd_power_info);
        break;
      }

      default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
  }

  void SetChargeState(bool charging) {
    role_ = charging ? USB_PD_PORT_POWER_SINK : USB_PD_PORT_POWER_SINK_NOT_CHARGING;
  }

  bool SupportsFeature(enum ec_feature_code feature) const override {
    return feature == EC_FEATURE_USB_PD;
  }

 private:
  usb_power_roles role_ = USB_PD_PORT_POWER_SINK_NOT_CHARGING;
};

class UsbPdTest : public zxtest::Test {
 public:
  void SetUp() override {
    ec_ = fbl::MakeRefCounted<FakeUsbPdEc>();

    ASSERT_OK(AcpiCrOsEcUsbPdDevice::Bind(fake_ddk::kFakeParent, ec_,
                                          cros_ec::CreateNoOpAcpiHandle(), &device_));

    client_ = fidl::WireSyncClient<fuchsia_hardware_power::Source>(
        ddk_.FidlClient<fuchsia_hardware_power::Source>());
  }

  void TearDown() override {
    device_->DdkAsyncRemove();
    device_->DdkRelease();
    ASSERT_TRUE(ddk_.Ok());
  }

 protected:
  fbl::RefPtr<FakeUsbPdEc> ec_;
  fake_ddk::Bind ddk_;
  AcpiCrOsEcUsbPdDevice* device_;
  fidl::WireSyncClient<fuchsia_hardware_power::Source> client_;
};

TEST_F(UsbPdTest, PowerInfo) {
  auto result = this->client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->status, ZX_OK);
  EXPECT_EQ(result->info.type, fuchsia_hardware_power::wire::PowerType::kAc);
  EXPECT_EQ(result->info.state, fuchsia_hardware_power::wire::kPowerStateDischarging);
}

TEST_F(UsbPdTest, PowerInfoCharging) {
  this->ec_->SetChargeState(true);

  auto result = this->client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->status, ZX_OK);
  EXPECT_EQ(result->info.type, fuchsia_hardware_power::wire::PowerType::kAc);
  EXPECT_EQ(result->info.state, fuchsia_hardware_power::wire::kPowerStateCharging);
}

TEST_F(UsbPdTest, BatteryInfo) {
  auto result = this->client_->GetBatteryInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(UsbPdTest, StateChangeEvent) {
  auto state_change_result = this->client_->GetStateChangeEvent();
  ASSERT_TRUE(state_change_result.ok());
  EXPECT_EQ(state_change_result->status, ZX_OK);

  zx_signals_t signals;
  zx::event event(std::move(state_change_result->handle));
  event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(0)), &signals);
  EXPECT_EQ(signals, 0);

  this->ec_->SetChargeState(true);
  this->device_->NotifyHandler(nullptr, 0x80, this->device_);

  event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(0)), &signals);
  EXPECT_EQ(signals, ZX_USER_SIGNAL_0);

  auto result = this->client_->GetPowerInfo();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->status, ZX_OK);
  EXPECT_EQ(result->info.state, fuchsia_hardware_power::wire::kPowerStateCharging);

  // Signal is cleared by call to GetPowerInfo.
  event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(0)), &signals);
  EXPECT_EQ(signals, 0);
}

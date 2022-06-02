// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/power_sensor.h"

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "chromiumos-platform-ec/ec_commands.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace chromiumos_ec_core::power_sensor {

class ChromiumosEcPowerSensorTest : public ChromiumosEcTestBase {
 public:
  void SetUp() override {
    ChromiumosEcTestBase::SetUp();
    ASSERT_OK(loop_.StartThread("cros-ec-power-sensor-test-fidl"));

    fake_ec_.SetBoard(kAtlasBoardName);

    fake_ec_.AddCommand(EC_CMD_ADC_READ, 0,
                        [this](const void* data, size_t data_size, auto& completer) {
                          IssueCommand(EC_CMD_ADC_READ, data, data_size, completer);
                        });

    // Calls DdkInit on the cros-ec-core device.
    ASSERT_NO_FATAL_FAILURE(InitDevice());

    // Initialise the power-sensor device.
    zx_device* powersensor_dev = ChromiumosEcTestBase::device_->zxdev()->GetLatestChild();
    powersensor_dev->InitOp();
    ASSERT_OK(powersensor_dev->WaitUntilInitReplyCalled(zx::time::infinite()));
    device_ = powersensor_dev->GetDeviceContext<CrOsEcPowerSensorDevice>();

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_power_sensor::Device>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), device_);
    client_.Bind(std::move(endpoints->client));
  }

  void IssueCommand(uint16_t command, const void* input, size_t input_size,
                    FakeEcDevice::RunCommandCompleter::Sync& completer) {
    using fuchsia_hardware_google_ec::wire::EcStatus;
    switch (command) {
      case EC_CMD_ADC_READ: {
        if (input_size < sizeof(ec_params_adc_read)) {
          completer.ReplyError(ZX_ERR_BUFFER_TOO_SMALL);
          return;
        }
        auto request = reinterpret_cast<const ec_params_adc_read*>(input);

        if (request->adc_channel != kAtlasAdcPsysChannel) {
          completer.ReplyError(ZX_ERR_IO);
          return;
        }

        ec_response_adc_read response{
            .adc_value = power_,
        };

        completer.ReplySuccess(EcStatus::kSuccess, MakeVectorView(response));
        break;
      }

      default:
        completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    }
  }

  void SetPower(int32_t power) { power_ = power; }

 protected:
  int32_t power_ = 15'000'000;
  CrOsEcPowerSensorDevice* device_;
  fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> client_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

TEST_F(ChromiumosEcPowerSensorTest, PowerInfo) {
  auto result = client_->GetPowerWatts();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_ok());
  EXPECT_EQ(result->value()->power, 15.0f);

  SetPower(20'500'000);
  auto result2 = client_->GetPowerWatts();
  ASSERT_TRUE(result2.ok());
  ASSERT_TRUE(result2->is_ok());
  EXPECT_EQ(result2->value()->power, 20.5f);

  SetPower(-1);
  auto result3 = client_->GetPowerWatts();
  ASSERT_TRUE(result3.ok());
  ASSERT_TRUE(result3->is_error());
  EXPECT_EQ(result3->error_value(), ZX_ERR_INTERNAL);
}

TEST_F(ChromiumosEcPowerSensorTest, VoltageInfo) {
  auto result = client_->GetVoltageVolts();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->error_value(), ZX_ERR_NOT_SUPPORTED);
}

}  // namespace chromiumos_ec_core::power_sensor

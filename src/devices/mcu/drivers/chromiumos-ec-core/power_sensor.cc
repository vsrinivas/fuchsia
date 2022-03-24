// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/power_sensor.h"

#include <lib/ddk/debug.h>
#include <lib/fpromise/promise.h>

#include <chromiumos-platform-ec/ec_commands.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/subdriver.h"

namespace chromiumos_ec_core::power_sensor {

void RegisterPowerSensorDriver(ChromiumosEcCore* ec) {
  CrOsEcPowerSensorDevice* device;
  zx_status_t status = CrOsEcPowerSensorDevice::Bind(ec->zxdev(), ec, &device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialise power-sensor device: %s", zx_status_get_string(status));
  }
}

zx_status_t CrOsEcPowerSensorDevice::Bind(zx_device_t* parent, ChromiumosEcCore* ec,
                                          CrOsEcPowerSensorDevice** device) {
  fbl::AllocChecker ac;
  std::unique_ptr<CrOsEcPowerSensorDevice> dev(new (&ac) CrOsEcPowerSensorDevice(ec, parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  ddk::DeviceAddArgs args("cros-ec-power-sensor");
  args.set_proto_id(ZX_PROTOCOL_POWER_SENSOR);
  zx_status_t status = dev->DdkAdd(args);
  if (status != ZX_OK) {
    return status;
  }

  // Ownership has transferred to the DDK, so release our unique_ptr, but
  // let the caller have a pointer to it.
  if (device != nullptr) {
    *device = dev.get();
  }
  (void)dev.release();

  return ZX_OK;
}

void CrOsEcPowerSensorDevice::DdkInit(ddk::InitTxn txn) {
  auto promise = UpdateState().then(
      [txn = std::move(txn)](fpromise::result<void, zx_status_t>& result) mutable {
        if (result.is_ok()) {
          txn.Reply(ZX_OK);
        } else {
          txn.Reply(result.take_error());
        }
      });

  ec_->executor().schedule_task(std::move(promise));
}

fpromise::promise<void, zx_status_t> CrOsEcPowerSensorDevice::UpdateState() {
  if (ec_->IsBoard(kAtlasBoardName)) {
    ec_params_adc_read request = {
        .adc_channel = kAtlasAdcPsysChannel,
    };

    return ec_->IssueCommand(EC_CMD_ADC_READ, 0, request)
        .and_then([this](CommandResult& result) -> fpromise::result<void, zx_status_t> {
          auto response = result.GetData<ec_response_adc_read>();

          auto new_power = static_cast<float>(response->adc_value) / 1'000'000;
          if (new_power <= 0) {
            zxlogf(ERROR, "EC returned negative or zero power usage");
            return fpromise::error(ZX_ERR_INTERNAL);
          }
          power_ = new_power;

          return fpromise::ok();
        });
  }

  return fpromise::make_error_promise(ZX_ERR_NOT_SUPPORTED);
}

void CrOsEcPowerSensorDevice::GetPowerWatts(GetPowerWattsRequestView request,
                                            GetPowerWattsCompleter::Sync& completer) {
  ec_->executor().schedule_task(UpdateState().then(
      [this, completer = completer.ToAsync()](fpromise::result<void, zx_status_t>& result) mutable {
        if (result.is_error()) {
          completer.ReplyError(result.take_error());
          return;
        }

        completer.ReplySuccess(power_);
      }));
}

void CrOsEcPowerSensorDevice::GetVoltageVolts(GetVoltageVoltsRequestView request,
                                              GetVoltageVoltsCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void CrOsEcPowerSensorDevice::DdkRelease() { delete this; }

}  // namespace chromiumos_ec_core::power_sensor

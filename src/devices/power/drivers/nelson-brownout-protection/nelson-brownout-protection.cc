// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nelson-brownout-protection.h"

#include <fuchsia/hardware/audio/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/power/sensor/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/zx/channel.h>
#include <zircon/threads.h>

#include <memory>

#include "src/devices/power/drivers/nelson-brownout-protection/nelson-brownout-protection-bind.h"

namespace {

constexpr zx::duration kVoltagePollInterval = zx::sec(5);
// AGL will be disabled once the voltage rises above this value.
constexpr float kVoltageUpwardThreshold = 11.5f;

}  // namespace

namespace brownout_protection {

zx_status_t NelsonBrownoutProtection::Create(void* ctx, zx_device_t* parent) {
  ddk::CodecProtocolClient codec(parent, "codec");
  if (!codec.is_valid()) {
    zxlogf(ERROR, "No codec fragment");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PowerSensorProtocolClient power_sensor(parent, "power-sensor");
  if (!power_sensor.is_valid()) {
    zxlogf(ERROR, "No power sensor fragment");
    return ZX_ERR_NO_RESOURCES;
  }

  zx::status power_sensor_endpoints =
      fidl::CreateEndpoints<fuchsia_hardware_power_sensor::Device>();
  if (!power_sensor_endpoints.is_ok()) {
    zxlogf(ERROR, "Failed to create channel: %s", power_sensor_endpoints.status_string());
    return power_sensor_endpoints.status_value();
  }
  fidl::WireSyncClient power_sensor_client =
      fidl::BindSyncClient(std::move(power_sensor_endpoints->client));

  zx_status_t status = power_sensor.ConnectServer(power_sensor_endpoints->server.TakeChannel());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to connect to power_sensor driver: %s", zx_status_get_string(status));
    return status;
  }

  ddk::GpioProtocolClient alert_gpio(parent, "alert-gpio");
  if (!alert_gpio.is_valid()) {
    zxlogf(ERROR, "No GPIO fragment");
    return ZX_ERR_NO_RESOURCES;
  }

  // Pulled up externally.
  if ((status = alert_gpio.ConfigIn(GPIO_NO_PULL)) != ZX_OK) {
    zxlogf(ERROR, "Failed to configure alert GPIO: %s", zx_status_get_string(status));
    return status;
  }

  zx::interrupt alert_interrupt;
  if ((status = alert_gpio.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &alert_interrupt)) != ZX_OK) {
    zxlogf(ERROR, "Failed to get alert interrupt: %s", zx_status_get_string(status));
    return status;
  }

  auto dev = std::make_unique<NelsonBrownoutProtection>(parent, std::move(power_sensor_client),
                                                        std::move(alert_interrupt));
  if ((status = dev->Init(codec)) != ZX_OK) {
    return status;
  }

  if ((status = dev->DdkAdd("nelson-brownout-protection")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t NelsonBrownoutProtection::Init(ddk::CodecProtocolClient codec) {
  zx_status_t status = codec_.SetProtocol(codec);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to connect to codec driver: %s", zx_status_get_string(status));
    return status;
  }

  zx::status<audio::GainFormat> gain_format = codec_.GetGainFormat();
  if (gain_format.is_error()) {
    zxlogf(ERROR, "Failed to get codec gain format: %s",
           zx_status_get_string(gain_format.status_value()));
    return gain_format.status_value();
  }
  default_gain_ = gain_format->min_gain;

  status = thrd_status_to_zx_status(thrd_create_with_name(
      &thread_,
      [](void* ctx) -> int { return reinterpret_cast<NelsonBrownoutProtection*>(ctx)->Thread(); },
      this, "Brownout protection thread"));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start thread: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

int NelsonBrownoutProtection::Thread() {
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    // AGL should be enabled at most 4ms after the power sensor raises an interrupt. The capacity
    // was chosen through experimentation -- too low and page faults end up using most of the time.
    // This is especially noticeable with the codec driver.
    constexpr zx::duration capacity = zx::msec(3);
    constexpr zx::duration deadline = zx::msec(4);
    constexpr zx::duration period = deadline;

    zx::profile profile;
    zx_status_t status =
        device_get_deadline_profile(parent_, capacity.get(), deadline.get(), period.get(),
                                    "Brownout protection profile", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to get deadline profile: %s", zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Failed to apply deadline profile: %s", zx_status_get_string(status));
      }
    }
  }

  zx::time timestamp = {};
  while (run_thread_ && alert_interrupt_.wait(&timestamp) == ZX_OK) {
    {
      TRACE_DURATION("brownout-protection", "Enable AGL", "timestamp", timestamp.get());
      zx_status_t status = codec_.SetAgl(true);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Failed to enable AGL: %s", zx_status_get_string(status));
      }
    }

    while (run_thread_) {
      zx::nanosleep(zx::deadline_after(kVoltagePollInterval));
      const auto result = power_sensor_->GetVoltageVolts();
      if (result.ok() && result.Unwrap_NEW()->value()->voltage >= kVoltageUpwardThreshold) {
        break;
      }
    }

    zx_status_t status = codec_.SetAgl(false);
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to disable AGL: %s", zx_status_get_string(status));
    }
  }

  return thrd_success;
}

static constexpr zx_driver_ops_t nelson_brownout_protection_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = NelsonBrownoutProtection::Create;
  return ops;
}();

}  // namespace brownout_protection

ZIRCON_DRIVER(nelson_brownout_protection,
              brownout_protection::nelson_brownout_protection_driver_ops, "zircon", "0.1");

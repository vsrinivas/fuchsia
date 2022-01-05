// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_NELSON_BROWNOUT_PROTECTION_NELSON_BROWNOUT_PROTECTION_H_
#define SRC_DEVICES_POWER_DRIVERS_NELSON_BROWNOUT_PROTECTION_NELSON_BROWNOUT_PROTECTION_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.sensor/cpp/wire.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <atomic>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace brownout_protection {

class NelsonBrownoutProtection;
using DeviceType = ddk::Device<NelsonBrownoutProtection>;

class NelsonBrownoutProtection : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  NelsonBrownoutProtection(zx_device_t* parent,
                           fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> power_sensor,
                           zx::interrupt alert_interrupt)
      : DeviceType(parent),
        power_sensor_(std::move(power_sensor)),
        alert_interrupt_(std::move(alert_interrupt)) {}
  ~NelsonBrownoutProtection() {
    alert_interrupt_.destroy();
    run_thread_ = false;
    thrd_join(thread_, nullptr);
  }

  void DdkRelease() { delete this; }

 private:
  zx_status_t Init(ddk::CodecProtocolClient codec);

  int Thread();

  thrd_t thread_;
  audio::SimpleCodecClient codec_;
  fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> power_sensor_;
  const zx::interrupt alert_interrupt_;
  std::atomic_bool run_thread_ = true;
  // The minimum gain supported by the codec, used only if we can't determine the current gain.
  float default_gain_ = 0.0f;
};

}  // namespace brownout_protection

#endif  // SRC_DEVICES_POWER_DRIVERS_NELSON_BROWNOUT_PROTECTION_NELSON_BROWNOUT_PROTECTION_H_

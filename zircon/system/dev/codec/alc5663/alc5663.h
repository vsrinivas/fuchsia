// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/device-protocol/i2c-channel.h>
#include <zircon/types.h>

#include "i2c_client.h"

namespace audio::alc5663 {

class Alc5663Device;
using DeviceType = ddk::Device<Alc5663Device, ddk::Unbindable>;

// ALC5663 uses 8-bit register addresses.
using Alc5663Client = I2cClient<uint8_t>;

class Alc5663Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
 public:
  // Create a new device. Caller retains ownership of raw pointer arguments.
  Alc5663Device(zx_device_t* parent, ddk::I2cChannel channel);
  ~Alc5663Device() = default;

  // Bind a new Alc5663 to its parent.
  //
  // The DDK gains ownership of the device until DdkRelease() is called.
  static zx_status_t Bind(fbl::unique_ptr<Alc5663Device> device);

  // Initialise the hardware.
  zx_status_t InitializeDevice();

  // Shutdown the hardware.
  void Shutdown();

  // Implementation of |ddk::Unbindable|.
  void DdkUnbind();
  void DdkRelease();

 private:
  Alc5663Client client_;
};

}  // namespace audio::alc5663

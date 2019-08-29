// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/device-protocol/i2c-channel.h>
#include <zircon/types.h>

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "i2c_client.h"

namespace audio::alc5663 {

class Alc5663Device;
using DeviceType = ddk::Device<Alc5663Device, ddk::Unbindable>;

// ALC5663 uses 16-bit register addresses.
using Alc5663Client = I2cClient<uint16_t>;

class Alc5663Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
 public:
  // Create a new device. Caller retains ownership of raw pointer arguments.
  Alc5663Device(zx_device_t* parent, ddk::I2cChannel channel);
  ~Alc5663Device() = default;

  // Create a new Alc5663Device object, and bind it to the given parent.
  //
  // The parent should expose an I2C protocol communicating with ALC5663 codec
  // hardware.
  //
  // On success, an unowned pointer to the created device will be returned in
  // |created_device|. Ownership of the pointer remains with the DDK.
  static zx_status_t Bind(zx_device_t* parent, Alc5663Device** created_device);

  // Add a created ALC5663 to its parent.
  //
  // The DDK gains ownership of the device until DdkRelease() is called.
  static zx_status_t AddChildToParent(fbl::unique_ptr<Alc5663Device> device);

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

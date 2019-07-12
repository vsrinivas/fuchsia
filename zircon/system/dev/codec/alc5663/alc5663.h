// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace audio {
namespace alc5663 {

class Alc5663Device;
using DeviceType = ddk::Device<Alc5663Device, ddk::Unbindable>;

class Alc5663Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
 public:
  ~Alc5663Device() = default;

  // Create and bind a new Alc5663 with the given parent.
  //
  // If provided, `result` will contain a pointer to the created object,
  // but the object is owned by the DDK.
  static zx_status_t CreateAndBind(zx_device_t* parent, Alc5663Device** result = nullptr);

  // Implementation of |ddk::Unbindable|.
  void DdkUnbind();
  void DdkRelease();

 private:
  explicit Alc5663Device(zx_device_t* parent) : DeviceType(parent) {}

  i2c_protocol_t i2c_;
};

}  // namespace alc5663
}  // namespace audio

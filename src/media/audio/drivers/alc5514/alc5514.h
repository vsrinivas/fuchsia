// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_ALC5514_ALC5514_H_
#define SRC_MEDIA_AUDIO_DRIVERS_ALC5514_ALC5514_H_

#include <zircon/types.h>

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace audio {
namespace alc5514 {

class Alc5514Device;
using DeviceType = ddk::Device<Alc5514Device, ddk::Unbindable>;

class Alc5514Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  Alc5514Device(zx_device_t* parent) : DeviceType(parent) {}
  ~Alc5514Device() {}

  zx_status_t Bind();
  zx_status_t Initialize();

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  void DumpRegs();

  // Methods to read/write registers
  uint32_t ReadReg(uint32_t addr);
  void WriteReg(uint32_t addr, uint32_t val);
  void UpdateReg(uint32_t addr, uint32_t mask, uint32_t bits);

  i2c_protocol_t i2c_;
};

}  // namespace alc5514
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_ALC5514_ALC5514_H_

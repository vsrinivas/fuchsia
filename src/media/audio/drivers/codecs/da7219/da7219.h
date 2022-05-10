// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_H_

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/async/cpp/irq.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <ddktl/device.h>

namespace audio {

class Da7219 : public SimpleCodecServer {
 public:
  explicit Da7219(zx_device_t* parent, fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c,
                  zx::interrupt irq);
  ~Da7219() override = default;

  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

 private:
  // Implementation for SimpleCodecServer.
  zx::status<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override;
  zx_status_t Start() override;
  DaiSupportedFormats GetDaiFormats() override;
  zx::status<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;

  void HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
                 const zx_packet_interrupt_t* interrupt);
  void PlugDetected(bool plugged);

  inspect::Inspector inspect_;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c_;
  zx::interrupt irq_;
  async::IrqMethod<Da7219, &Da7219::HandleIrq> irq_handler_{this};
  bool plugged_ = false;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_H_

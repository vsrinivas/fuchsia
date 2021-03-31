// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AML_TDM_CONFIG_DEVICE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AML_TDM_CONFIG_DEVICE_H_

#include <memory>

#include <soc/aml-common/aml-tdm-audio.h>

namespace audio::aml_g12 {

class AmlTdmConfigDevice {
 public:
  static constexpr uint32_t kDefaultFrameRateIndex = 3;
  static constexpr uint32_t kSupportedFrameRates[] = {8'000, 16'000, 32'000, 48'000, 96'000};
  explicit AmlTdmConfigDevice(const metadata::AmlConfig& metadata, ddk::MmioBuffer mmio);

  zx_status_t InitHW(const metadata::AmlConfig& metadata, uint64_t channels_to_use,
                     uint32_t frame_rate);
  static zx_status_t Normalize(metadata::AmlConfig& metadata);

  zx_status_t SetBuffer(zx_paddr_t buf, size_t len) { return device_->SetBuffer(buf, len); }
  uint32_t GetRingPosition() { return device_->GetRingPosition(); }
  uint32_t GetDmaStatus() { return device_->GetDmaStatus(); }
  uint32_t GetTdmStatus() { return device_->GetTdmStatus(); }
  uint64_t Start() { return device_->Start(); }
  void Stop() { device_->Stop(); }
  uint32_t fifo_depth() const { return device_->fifo_depth(); }
  uint32_t GetBufferAlignment() const { return device_->GetBufferAlignment(); }
  void Shutdown() { device_->Shutdown(); }

 private:
  std::unique_ptr<AmlTdmDevice> device_;
};

}  // namespace audio::aml_g12

#endif  // SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AML_TDM_CONFIG_DEVICE_H_

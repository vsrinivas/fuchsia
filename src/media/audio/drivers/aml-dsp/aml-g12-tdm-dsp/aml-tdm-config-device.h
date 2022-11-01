// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_AML_DSP_AML_G12_TDM_DSP_AML_TDM_CONFIG_DEVICE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_AML_DSP_AML_G12_TDM_DSP_AML_TDM_CONFIG_DEVICE_H_

#include <memory>

#include <soc/aml-common/aml-tdm-audio.h>

namespace audio::aml_g12 {

class AmlTdmConfigDevice {
 public:
  static constexpr uint32_t kDefaultFrameRateIndex = 3;
  static constexpr uint32_t kSupportedFrameRates[] = {8'000, 16'000, 32'000, 48'000, 96'000};
  explicit AmlTdmConfigDevice(const metadata::AmlConfig& metadata, fdf::MmioBuffer mmio);

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

  static aml_tdm_mclk_t ToMclkId(const metadata::AmlTdmclk clk) {
    switch (clk) {
      case metadata::AmlTdmclk::CLK_A:
        return MCLK_A;
      case metadata::AmlTdmclk::CLK_B:
        return MCLK_B;
      case metadata::AmlTdmclk::CLK_C:
        return MCLK_C;
      case metadata::AmlTdmclk::CLK_D:
        return MCLK_D;
      case metadata::AmlTdmclk::CLK_E:
        return MCLK_E;
      case metadata::AmlTdmclk::CLK_F:
        return MCLK_F;
    }
    assert(0);
    return MCLK_A;
  }

  static aml_tdm_mclk_pad_t ToMclkPadId(const metadata::AmlTdmMclkPad mpad) {
    switch (mpad) {
      case metadata::AmlTdmMclkPad::MCLK_PAD_0:
        return MCLK_PAD_0;
      case metadata::AmlTdmMclkPad::MCLK_PAD_1:
        return MCLK_PAD_1;
      case metadata::AmlTdmMclkPad::MCLK_PAD_2:
        return MCLK_PAD_2;
    }
    assert(0);
    return MCLK_PAD_0;
  }

  static aml_tdm_sclk_pad_t ToSclkPadId(const metadata::AmlTdmSclkPad spad) {
    switch (spad) {
      case metadata::AmlTdmSclkPad::SCLK_PAD_0:
        return SCLK_PAD_0;
      case metadata::AmlTdmSclkPad::SCLK_PAD_1:
        return SCLK_PAD_1;
      case metadata::AmlTdmSclkPad::SCLK_PAD_2:
        return SCLK_PAD_2;
    }
    assert(0);
    return SCLK_PAD_0;
  }

  static aml_tdm_dat_pad_t ToDatPadId(const metadata::AmlTdmDatPad pad) {
    switch (pad) {
      case metadata::AmlTdmDatPad::TDM_D4:
        return TDM_D4;
      case metadata::AmlTdmDatPad::TDM_D5:
        return TDM_D5;
      case metadata::AmlTdmDatPad::TDM_D8:
        return TDM_D8;
      case metadata::AmlTdmDatPad::TDM_D9:
        return TDM_D9;
      case metadata::AmlTdmDatPad::TDM_D10:
        return TDM_D10;
      case metadata::AmlTdmDatPad::TDM_D11:
        return TDM_D11;
    }
    assert(0);
    return TDM_D4;
  }

 private:
  std::unique_ptr<AmlTdmDevice> device_;
};

}  // namespace audio::aml_g12

#endif  // SRC_MEDIA_AUDIO_DRIVERS_AML_DSP_AML_G12_TDM_DSP_AML_TDM_CONFIG_DEVICE_H_

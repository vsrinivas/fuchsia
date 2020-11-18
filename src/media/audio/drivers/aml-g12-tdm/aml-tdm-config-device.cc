// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "aml-tdm-config-device.h"

#include <utility>

#include <ddk/debug.h>
#include <fbl/auto_call.h>

namespace audio::aml_g12 {

AmlTdmConfigDevice::AmlTdmConfigDevice(const metadata::AmlConfig& metadata, ddk::MmioBuffer mmio) {
  if (metadata.is_input) {
    aml_tdm_in_t tdm = {};
    aml_toddr_t ddr = {};
    aml_tdm_mclk_t mclk = {};
    switch (metadata.bus) {
      case metadata::AmlBus::TDM_A:
        tdm = TDM_IN_A;
        ddr = TODDR_A;
        mclk = MCLK_A;
        break;
      case metadata::AmlBus::TDM_B:
        tdm = TDM_IN_B;
        ddr = TODDR_B;
        mclk = MCLK_B;
        break;
      case metadata::AmlBus::TDM_C:
        tdm = TDM_IN_C;
        ddr = TODDR_C;
        mclk = MCLK_C;
        break;
    }
    device_ = AmlTdmInDevice::Create(std::move(mmio), HIFI_PLL, tdm, ddr, mclk, metadata.version);
  } else {
    aml_tdm_out_t tdm = {};
    aml_frddr_t ddr = {};
    aml_tdm_mclk_t mclk = {};
    switch (metadata.bus) {
      case metadata::AmlBus::TDM_A:
        tdm = TDM_OUT_A;
        ddr = FRDDR_A;
        mclk = MCLK_A;
        break;
      case metadata::AmlBus::TDM_B:
        tdm = TDM_OUT_B;
        ddr = FRDDR_B;
        mclk = MCLK_B;
        break;
      case metadata::AmlBus::TDM_C:
        tdm = TDM_OUT_C;
        ddr = FRDDR_C;
        mclk = MCLK_C;
        break;
    }
    device_ = AmlTdmOutDevice::Create(std::move(mmio), HIFI_PLL, tdm, ddr, mclk, metadata.version);
  }
  ZX_ASSERT(device_ != nullptr);
}

zx_status_t AmlTdmConfigDevice::InitHW(const metadata::AmlConfig& metadata,
                                       uint64_t channels_to_use, uint32_t frame_rate) {
  zx_status_t status;

  // Shut down the SoC audio peripherals (tdm/dma)
  device_->Shutdown();

  auto on_error = fbl::MakeAutoCall([this]() { device_->Shutdown(); });

  device_->Initialize();

  // Setup TDM.
  constexpr uint32_t kMaxLanes = metadata::kMaxNumberOfLanes;
  uint32_t lanes_mutes[kMaxLanes] = {};
  // bitoffset defines samples start relative to the edge of fsync.
  uint8_t bitoffset = metadata.is_input ? 4 : 3;
  if (metadata.tdm.type == metadata::TdmType::I2s) {
    bitoffset--;
  }
  if (metadata.tdm.sclk_on_raising) {
    bitoffset--;
  }

  // Configure lanes mute masks based on channels_to_use and lane enable mask.
  if (channels_to_use != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED) {
    uint32_t channel = 0;
    size_t lane_start = 0;
    for (size_t i = 0; i < kMaxLanes; ++i) {
      for (size_t j = 0; j < 64; ++j) {
        if (metadata.lanes_enable_mask[i] & (static_cast<uint64_t>(1) << j)) {
          if (~channels_to_use & (1 << channel)) {
            lanes_mutes[i] |= ((1 << channel) >> lane_start);
          }
          channel++;
        }
      }
      lane_start = channel;
    }
  }
  device_->ConfigTdmSlot(bitoffset, static_cast<uint8_t>(metadata.dai_number_of_channels - 1),
                         metadata.tdm.bits_per_slot - 1, metadata.tdm.bits_per_sample - 1,
                         metadata.mix_mask, metadata.tdm.type == metadata::TdmType::I2s);
  device_->ConfigTdmSwaps(metadata.swaps);
  for (size_t i = 0; i < kMaxLanes; ++i) {
    status = device_->ConfigTdmLane(i, metadata.lanes_enable_mask[i], lanes_mutes[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure TDM lane %d", __FILE__, status);
      return status;
    }
  }

  if (metadata.mClockDivFactor) {
    // PLL sourcing audio clock tree should be running at 768MHz
    // Note: Audio clock tree input should always be < 1GHz
    // mclk rate for 96kHz = 768MHz/5 = 153.6MHz
    // mclk rate for 48kHz = 768MHz/10 = 76.8MHz
    // Note: absmax mclk frequency is 500MHz per AmLogic
    ZX_ASSERT(!(metadata.mClockDivFactor % 2));  // mClock div factor must be divisable by 2.
    ZX_ASSERT(frame_rate == 48'000 || frame_rate == 96'000);
    static_assert(countof(AmlTdmConfigDevice::kSupportedFrameRates) == 2);
    static_assert(AmlTdmConfigDevice::kSupportedFrameRates[0] == 48'000);
    static_assert(AmlTdmConfigDevice::kSupportedFrameRates[1] == 96'000);
    uint32_t mdiv = metadata.mClockDivFactor / ((frame_rate == 96'000) ? 2 : 1);
    status = device_->SetMclkDiv(mdiv - 1);  // register val is div - 1;
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
      return status;
    }
    device_->SetMClkPad(MCLK_PAD_0);
  }
  if (metadata.sClockDivFactor) {
    uint32_t frame_sync_clks = 0;
    switch (metadata.tdm.type) {
      case metadata::TdmType::I2s:
      case metadata::TdmType::StereoLeftJustified:
        // For I2S and Stereo Left Justified we have a 50% duty cycle, hence the frame sync clocks
        // is set to the size of one slot.
        frame_sync_clks = metadata.tdm.bits_per_slot;
        break;
      case metadata::TdmType::Tdm1:
        frame_sync_clks = 1;
        break;
    }
    status = device_->SetSclkDiv(metadata.sClockDivFactor - 1, frame_sync_clks - 1,
                                 (metadata.tdm.bits_per_slot * metadata.dai_number_of_channels) - 1,
                                 !metadata.tdm.sclk_on_raising);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
      return status;
    }
  }

  // Allow clock divider changes to stabilize
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

  device_->Sync();

  on_error.cancel();
  // At this point the SoC audio peripherals are ready to start, but no
  //  clocks are active.  The codec is also in software shutdown and will
  //  need to be started after the audio clocks are activated.
  return ZX_OK;
}

zx_status_t AmlTdmConfigDevice::Normalize(metadata::AmlConfig& metadata) {
  // Only the PCM signed sample format is supported.
  if (metadata.tdm.sample_format != metadata::SampleFormat::PcmSigned) {
    zxlogf(ERROR, "%s metadata unsupported sample type %d", __FILE__,
           static_cast<int>(metadata.tdm.sample_format));
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.tdm.type == metadata::TdmType::I2s ||
      metadata.tdm.type == metadata::TdmType::StereoLeftJustified) {
    metadata.dai_number_of_channels = 2;
  }
  if (metadata.tdm.bits_per_sample == 0) {
    metadata.tdm.bits_per_sample = 16;
  }
  if (metadata.tdm.bits_per_slot == 0) {
    metadata.tdm.bits_per_slot = 32;
  }
  if (metadata.tdm.bits_per_slot != 32 && metadata.tdm.bits_per_slot != 16) {
    zxlogf(ERROR, "%s metadata unsupported bits per slot %d", __FILE__, metadata.tdm.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.tdm.bits_per_sample != 32 && metadata.tdm.bits_per_sample != 16) {
    zxlogf(ERROR, "%s metadata unsupported bits per sample %d", __FILE__,
           metadata.tdm.bits_per_sample);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.tdm.bits_per_sample > metadata.tdm.bits_per_slot) {
    zxlogf(ERROR, "%s metadata unsupported bits per sample bits per slot combination %u/%u",
           __FILE__, metadata.tdm.bits_per_sample, metadata.tdm.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace audio::aml_g12

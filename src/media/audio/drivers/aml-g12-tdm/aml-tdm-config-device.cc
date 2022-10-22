// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "aml-tdm-config-device.h"

#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>

#include <iterator>
#include <utility>

namespace audio::aml_g12 {

AmlTdmConfigDevice::AmlTdmConfigDevice(const metadata::AmlConfig& metadata, fdf::MmioBuffer mmio) {
  ee_audio_mclk_src_t mclk_src = {};
  if (metadata.is_custom_tdm_src_clk_sel) {
    mclk_src = MP0_PLL;
  } else {
    mclk_src = HIFI_PLL;
  }

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

    if (metadata.is_custom_tdm_clk_sel)
      mclk = ToMclkId(metadata.tdm_clk_sel);

    if (metadata.is_loopback) {
      device_ = AmlTdmLbDevice::Create(std::move(mmio), mclk_src, ddr, mclk, metadata.loopback,
                                       metadata.version);
    } else {
      device_ = AmlTdmInDevice::Create(std::move(mmio), mclk_src, tdm, ddr, mclk, metadata.version);
    }
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

    if (metadata.is_custom_tdm_clk_sel)
      mclk = ToMclkId(metadata.tdm_clk_sel);

    device_ = AmlTdmOutDevice::Create(std::move(mmio), mclk_src, tdm, ddr, mclk, metadata.version);
  }
  ZX_ASSERT(device_ != nullptr);
}

zx_status_t AmlTdmConfigDevice::InitHW(const metadata::AmlConfig& metadata,
                                       uint64_t channels_to_use, uint32_t frame_rate) {
  zx_status_t status;

  // Shut down the SoC audio peripherals (tdm/dma)
  device_->Shutdown();

  auto on_error = fit::defer([this]() { device_->Shutdown(); });

  device_->Initialize();

  // Setup TDM.
  constexpr uint32_t kMaxLanes = metadata::kMaxNumberOfLanes;
  uint32_t lanes_mutes[kMaxLanes] = {};
  // bitoffset defines samples start relative to the edge of fsync.
  uint8_t bitoffset = metadata.is_input ? 4 : 3;
  switch (metadata.dai.type) {
    // No change, data already starts at the frame sync start.
    case metadata::DaiType::Tdm1:
      [[fallthrough]];
    case metadata::DaiType::StereoLeftJustified:
      break;

      // One clk delta, data starts one sclk after frame sync start.
    case metadata::DaiType::Tdm2:
      [[fallthrough]];
    case metadata::DaiType::I2s:
      bitoffset--;
      break;

      // Two clks delta, data starts two sclks after frame sync start.
    case metadata::DaiType::Tdm3:
      bitoffset -= 2;
      break;
  }
  if (metadata.dai.sclk_on_raising) {
    bitoffset--;
  }

  // Configure lanes mute masks based on channels_to_use and lane enable mask.
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
  // Number of channels enabled in lanes must match the number of channels in the ring buffer.
  // If some weird configuration requires this constrain to not be true, remove this check.
  // Most configurations would be an error if these did not match.
  ZX_ASSERT(channel == metadata.ring_buffer.number_of_channels);

  device_->ConfigTdmSlot(bitoffset, static_cast<uint8_t>(metadata.dai.number_of_channels - 1),
                         metadata.dai.bits_per_slot - 1, metadata.dai.bits_per_sample - 1,
                         metadata.mix_mask, metadata.dai.type == metadata::DaiType::I2s);
  device_->ConfigTdmSwaps(metadata.swaps);
  for (size_t i = 0; i < kMaxLanes; ++i) {
    status = device_->ConfigTdmLane(i, metadata.lanes_enable_mask[i], lanes_mutes[i]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not configure TDM lane %d", status);
      return status;
    }

    if (metadata.dpad_mask & (1 << i)) {
      device_->SetDatPad(ToDatPadId(metadata.dpad_sel[i]), static_cast<aml_tdm_dat_lane_t>(i));
    }
  }

  if (metadata.mClockDivFactor) {
    // PLL sourcing audio clock tree should be running at 768MHz
    // Note: Audio clock tree input should always be < 1GHz
    // mclk rate for 96kHz = 768MHz/5 = 153.6MHz
    // mclk rate for 48kHz = 768MHz/10 = 76.8MHz
    // Note: absmax mclk frequency is 500MHz per AmLogic
    ZX_ASSERT(!(metadata.mClockDivFactor % 2));  // mClock div factor must be divisable by 2.
    ZX_ASSERT(frame_rate == 8'000 || frame_rate == 16'000 || frame_rate == 32'000 ||
              frame_rate == 48'000 || frame_rate == 96'000);
    static_assert(std::size(AmlTdmConfigDevice::kSupportedFrameRates) == 5);
    ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[0] == 8'000);
    ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[1] == 16'000);
    ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[2] == 32'000);
    ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[3] == 48'000);
    ZX_ASSERT(AmlTdmConfigDevice::kSupportedFrameRates[4] == 96'000);
    const uint32_t frame_bytes = metadata.dai.bits_per_slot / 8 * metadata.dai.number_of_channels;
    // With frame_bytes = 8, we take mClockDivFactor and adjust the mclk_div up or down from 48kHz.
    const uint32_t mclk_div = metadata.mClockDivFactor * 48'000 * 8 / frame_bytes / frame_rate;
    status = device_->SetMclkDiv(mclk_div - 1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not configure MCLK %d", status);
      return status;
    }
    if (metadata.is_custom_tdm_mpad_sel) {
      device_->SetMClkPad(ToMclkPadId(metadata.mpad_sel));
    } else {
      device_->SetMClkPad(MCLK_PAD_0);
    }
  }
  if (metadata.sClockDivFactor) {
    uint32_t frame_sync_clks = 0;
    switch (metadata.dai.type) {
      case metadata::DaiType::I2s:
        [[fallthrough]];
      case metadata::DaiType::StereoLeftJustified:
        // For I2S and Stereo Left Justified we have a 50% duty cycle, hence the frame sync clocks
        // is set to the size of one slot.
        frame_sync_clks = metadata.dai.bits_per_slot;
        break;
      case metadata::DaiType::Tdm1:
        [[fallthrough]];
      case metadata::DaiType::Tdm2:
        [[fallthrough]];
      case metadata::DaiType::Tdm3:
        frame_sync_clks = 1;
        break;
    }
    device_->SetSclkPad(ToSclkPadId(metadata.spad_sel), metadata.is_custom_tdm_spad_sel);
    status = device_->SetSclkDiv(metadata.sClockDivFactor - 1, frame_sync_clks - 1,
                                 (metadata.dai.bits_per_slot * metadata.dai.number_of_channels) - 1,
                                 !metadata.dai.sclk_on_raising);
    if (status != ZX_OK) {
      zxlogf(ERROR, "could not configure SCLK %d", status);
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
  if (metadata.ring_buffer.bytes_per_sample == 0) {
    metadata.ring_buffer.bytes_per_sample = 2;
  }
  // Only 16 bits samples supported.
  if (metadata.ring_buffer.bytes_per_sample != 2) {
    zxlogf(ERROR, "metadata unsupported bytes per sample %u",
           metadata.ring_buffer.bytes_per_sample);
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Only the PCM signed sample format is supported.
  if (metadata.dai.sample_format != metadata::SampleFormat::PcmSigned) {
    zxlogf(ERROR, "metadata unsupported sample type %d",
           static_cast<int>(metadata.dai.sample_format));
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.dai.type == metadata::DaiType::I2s ||
      metadata.dai.type == metadata::DaiType::StereoLeftJustified) {
    metadata.dai.number_of_channels = 2;
  }
  if (metadata.dai.bits_per_slot != 32 && metadata.dai.bits_per_slot != 16) {
    zxlogf(ERROR, "metadata unsupported bits per slot %d", metadata.dai.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.dai.bits_per_sample != 32 && metadata.dai.bits_per_sample != 16) {
    zxlogf(ERROR, "metadata unsupported bits per sample %d", metadata.dai.bits_per_sample);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (metadata.dai.bits_per_sample > metadata.dai.bits_per_slot) {
    zxlogf(ERROR, "metadata unsupported bits per sample bits per slot combination %u/%u",
           metadata.dai.bits_per_sample, metadata.dai.bits_per_slot);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace audio::aml_g12

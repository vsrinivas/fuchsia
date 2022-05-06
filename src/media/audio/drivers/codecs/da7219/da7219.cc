// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/codecs/da7219/da7219.h"

#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include "src/devices/lib/acpi/client.h"
#include "src/media/audio/drivers/codecs/da7219/da7219-bind.h"
#include "src/media/audio/drivers/codecs/da7219/da7219-regs.h"

namespace audio {

static const std::vector<uint32_t> kSupportedDaiNumberOfChannels = {2};
static const std::vector<SampleFormat> kSupportedDaiSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedDaiFrameFormats = {FrameFormat::I2S};
// As secondary, the system clock tracks WCLK (Sampling Rate Matching SRM mode).
static const std::vector<uint32_t> kSupportedDaiRates = {
    8'000, 11'025, 12'000, 16'000, 22'050, 24'000, 32'000, 44'100, 48'000, 88'200, 96'000};
// In I2S all these bits per slot/sample are supported.
static const std::vector<uint8_t> kSupportedDaiBitsPerSlot = {16, 20, 24, 32};
static const std::vector<uint8_t> kSupportedDaiBitsPerSample = {16, 20, 24, 32};
static const audio::DaiSupportedFormats kSupportedDaiDaiFormats = {
    .number_of_channels = kSupportedDaiNumberOfChannels,
    .sample_formats = kSupportedDaiSampleFormats,
    .frame_formats = kSupportedDaiFrameFormats,
    .frame_rates = kSupportedDaiRates,
    .bits_per_slot = kSupportedDaiBitsPerSlot,
    .bits_per_sample = kSupportedDaiBitsPerSample,
};

zx_status_t Da7219::Shutdown() { return ZX_OK; }

zx::status<DriverIds> Da7219::Initialize() {
  auto chip_id1 = ChipId1::Read(i2c_);
  if (!chip_id1.is_ok())
    return zx::error(chip_id1.status_value());

  auto chip_id2 = ChipId2::Read(i2c_);
  if (!chip_id2.is_ok())
    return zx::error(chip_id2.status_value());

  auto chip_revision = ChipRevision::Read(i2c_);
  if (!chip_revision.is_ok())
    return zx::error(chip_revision.status_value());

  constexpr uint8_t kSupportedChipId1 = 0x23;
  constexpr uint8_t kSupportedChipId2 = 0x93;
  if (chip_id1->chip_id1() != kSupportedChipId1 || chip_id2->chip_id2() != kSupportedChipId2) {
    zxlogf(ERROR, "Found not supported CHIP ids 0x%02X:0x%02X", chip_id1->chip_id1(),
           chip_id2->chip_id2());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  zxlogf(INFO, "Found device ID:0x%02X/0x%02X REV:0x%01X/0x%01X", chip_id1->chip_id1(),
         chip_id2->chip_id2(), chip_revision->chip_major(), chip_revision->chip_minor());

  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_DIALOG,
      .device_id = PDEV_DID_DIALOG_DA7219,
  });
}

zx_status_t Da7219::Reset() {
  zx_status_t status = SystemActive::Get().set_system_active(true).Write(i2c_);
  if (status != ZX_OK)
    return status;

  status = PllCtrl::Get()
               .set_pll_mode(2)  // Sampling Rate Matching SRM mode.
                                 // The PLL is enabled, and the system clock tracks WCLK.
               .set_pll_mclk_sqr_en(false)
               .set_pll_indiv(3)  // 18 to 36 MHz.
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // The HP amplifiers are configured to operate in true-ground (Charge Pump) mode.
  status = CpCtrl::Get()
               .set_cp_en(true)
               .set_cp_mchange(2)  // Voltage level is controlled by the DAC volume level.
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Routing, enable headphones.
  status = MixoutLSelect::Get().set_mixout_l_mix_select(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutRSelect::Get().set_mixout_r_mix_select(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutLCtrl::Get().set_mixout_l_amp_en(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutRCtrl::Get().set_mixout_r_amp_en(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = HpLCtrl::Get()
               .set_hp_l_amp_en(true)  // HP_L_AMP amplifier control.
               .set_hp_l_amp_mute_en(false)
               .set_hp_l_amp_ramp_en(false)
               .set_hp_l_amp_zc_en(false)
               .set_hp_l_amp_oe(true)  // Output control, output is driven.
               .set_hp_l_amp_min_gain_en(false)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = HpRCtrl::Get()
               .set_hp_r_amp_en(true)  // HP_R_AMP amplifier control.
               .set_hp_r_amp_mute_en(false)
               .set_hp_r_amp_ramp_en(false)
               .set_hp_r_amp_zc_en(false)
               .set_hp_r_amp_oe(true)  // Output control, output is driven.
               .set_hp_r_amp_min_gain_en(false)
               .Write(i2c_);
  return status;
}

Info Da7219::GetInfo() {
  return {.unique_id = "", .manufacturer = "Dialog", .product_name = "DA7219"};
}

zx_status_t Da7219::Stop() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Da7219::Start() {
  return ZX_OK;  // Always started.
}

DaiSupportedFormats Da7219::GetDaiFormats() { return kSupportedDaiDaiFormats; }

GainFormat Da7219::GetGainFormat() {
  return {
      .min_gain = 0,
      .max_gain = 0,
      .gain_step = 0,
      .can_mute = false,
      .can_agc = false,
  };
}

void Da7219::SetGainState(GainState gain_state) {}

GainState Da7219::GetGainState() { return {}; }

zx::status<CodecFormatInfo> Da7219::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiDaiFormats)) {
    zxlogf(ERROR, "Unsupported format");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  uint8_t dai_word_length = 0;
  // clang-format off
  switch (format.bits_per_sample) {
    case 16: dai_word_length = 0; break;
    case 20: dai_word_length = 1; break;
    case 24: dai_word_length = 2; break;
    case 32: dai_word_length = 3; break;
    default: return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  // clang-format on

  zx_status_t status = DaiTdmCtrl::Get()
                           .set_dai_tdm_mode_en(false)  // Mode set is I2S, not TDM.
                           .set_dai_oe(false)
                           .set_dai_tdm_ch_en(false)
                           .Write(i2c_);
  if (status != ZX_OK)
    return zx::error(status);
  status = DaiCtrl::Get()
               .set_dai_en(true)
               .set_dai_ch_num(2)  // Left and right channels are enabled.
               .set_dai_word_length(dai_word_length)
               .set_dai_format(0)  // I2S.
               .Write(i2c_);
  if (status != ZX_OK)
    return zx::error(status);
  return zx::ok(CodecFormatInfo{});
}

zx_status_t Da7219::Bind(void* ctx, zx_device_t* dev) {
  auto client = acpi::Client::Create(dev);
  if (client.is_ok()) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    if (endpoints.is_error()) {
      zxlogf(ERROR, "Failed to create I2C endpoints");
      return endpoints.error_value();
    }

    zx_status_t status = device_connect_fragment_fidl_protocol(
        dev, "i2c000", fidl::DiscoverableProtocolName<fuchsia_hardware_i2c::Device>,
        endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Could not get i2c protocol");
      return ZX_ERR_NO_RESOURCES;
    }

    return SimpleCodecServer::CreateAndAddToDdk<Da7219>(dev, std::move(endpoints->client));
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void Da7219::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Da7219::DdkRelease() { delete this; }

static zx_driver_ops_t da7219_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Da7219::Bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(Da7219, audio::da7219_driver_ops, "zircon", "0.1");

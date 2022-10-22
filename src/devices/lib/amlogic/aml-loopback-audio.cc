// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <fbl/alloc_checker.h>
#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-loopback-audio.h>

namespace audio::aml_g12 {

constexpr zx_off_t kLoopbackOffset = EE_AUDIO_LB_A_CTRL0;
constexpr zx_off_t kLoopbackSize = 4 * sizeof(uint32_t);

// LOOPBACK Registers.
class LbCtrl : public hwreg::RegisterBase<LbCtrl, uint32_t> {
 public:
  // For Common Chips.
  DEF_BIT(31, enable);
  DEF_BIT(30, mode);
  // |Datain| packet_format, msb, lsb.
  DEF_FIELD(15, 13, packet_format);
  DEF_FIELD(12, 8, msb);
  DEF_FIELD(7, 3, lsb);

  // For S905D2.
  static bool has_datain_channel_sel(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kS905D2G;
  }
  // Max channel number of |Datain| source.
  DEF_FIELD(26, 24, datain_channel_nums);
  // Active channel mask of |Datain| source.
  DEF_FIELD(23, 16, datain_channel_mask);

  // For S905D2/S905D3.
  static bool has_datain_src(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kS905D2G || version == metadata::AmlVersion::kS905D3G;
  }
  // Source for LOOPBACK |Datain|.
  DEF_FIELD(2, 0, datain_src);

  static auto Get() { return hwreg::RegisterAddr<LbCtrl>(LB_A_CTRL0_OFFS); }
};

class LbCtrl1 : public hwreg::RegisterBase<LbCtrl1, uint32_t> {
 public:
  // For Common Chips.
  // |Datalb| packet_format, msb, lsb.
  DEF_FIELD(15, 13, packet_format);
  DEF_FIELD(12, 8, msb);
  DEF_FIELD(7, 3, lsb);

  // For S905D2.
  static bool has_datalb_channel_sel(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kS905D2G;
  }
  // Max channel number of |Datalb| source.
  DEF_FIELD(26, 24, datalb_channel_nums);
  // Active channel mask of |Datalb| source.
  DEF_FIELD(23, 16, datalb_channel_mask);

  // For S905D2/S905D3.
  static bool has_datalb_src(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kS905D2G || version == metadata::AmlVersion::kS905D3G;
  }
  // Source for LOOPBACK |Datalb|.
  DEF_FIELD(2, 0, datalb_src);

  static auto Get() { return hwreg::RegisterAddr<LbCtrl1>(LB_A_CTRL1_OFFS); }
};

class LbCtrl2 : public hwreg::RegisterBase<LbCtrl2, uint32_t> {
 public:
  // For S905D3/A5.
  static bool has_datain_channel_sel(metadata::AmlVersion version) {
    return version != metadata::AmlVersion::kS905D2G;
  }
  // Max channel number of |Datain| source.
  DEF_FIELD(19, 16, datain_channel_nums);
  // Active channel mask of |Datain| source.
  DEF_FIELD(15, 0, datain_channel_mask);

  // For A5.
  static bool has_datain_src(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kA5;
  }
  // Source for LOOPBACK |Datain|.
  DEF_FIELD(24, 20, datain_src);

  static auto Get() { return hwreg::RegisterAddr<LbCtrl2>(LB_A_CTRL2_OFFS); }
};

class LbCtrl3 : public hwreg::RegisterBase<LbCtrl3, uint32_t> {
 public:
  // For S905D3/A5.
  static bool has_datalb_channel_sel(metadata::AmlVersion version) {
    return version != metadata::AmlVersion::kS905D2G;
  }
  // Max channel number of |Datalb| source.
  DEF_FIELD(19, 16, datalb_channel_nums);
  // Active channel mask of |Datalb| source.
  DEF_FIELD(15, 0, datalb_channel_mask);

  // For A5.
  static bool has_datalb_src(metadata::AmlVersion version) {
    return version == metadata::AmlVersion::kA5;
  }
  // Source for LOOPBACK |Datalb|.
  DEF_FIELD(24, 20, datalb_src);

  static auto Get() { return hwreg::RegisterAddr<LbCtrl3>(LB_A_CTRL3_OFFS); }
};
// End of LOOPBACK Registers

// static
std::unique_ptr<AmlLoopbackDevice> AmlLoopbackDevice::Create(const fdf::MmioBuffer& mmio,
                                                             const metadata::AmlVersion version,
                                                             metadata::AmlLoopbackConfig config) {
  ZX_ASSERT_MSG((kLoopbackOffset + kLoopbackSize) < mmio.get_size(), "Out Of Mmio Region.");

  fdf::MmioView view = mmio.View(kLoopbackOffset, kLoopbackSize);
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<AmlLoopbackDevice>(
      new (&ac) AmlLoopbackDevice(std::move(view), version, config));
  if (!ac.check()) {
    zxlogf(ERROR, "Could not Create AmlLoopbackDevice.");
    return nullptr;
  }

  ZX_ASSERT(dev->Initialize() == ZX_OK);
  zxlogf(INFO, "Create AmlLoopbackDevice Successfully.");

  return dev;
}

zx_status_t AmlLoopbackDevice::Initialize() {
  if (datalb_chnum_ && !datain_chnum_) {
    LbRateMode(true);
  } else {
    LbRateMode(false);
  }

  if (datain_chnum_) {
    ConfigDataIn(datalb_chnum_, datain_chmask_, datain_src_);
  }

  if (datalb_chnum_) {
    ConfigDataLb(datalb_chnum_, datalb_chmask_);
  }

  return ZX_OK;
}

void AmlLoopbackDevice::LbRateMode(bool is_lb_rate) {
  LbCtrl::Get().ReadFrom(&view_).set_mode(is_lb_rate).WriteTo(&view_);
}

zx_status_t AmlLoopbackDevice::ConfigDataIn(uint32_t active_channels, uint32_t enable_mask,
                                            uint32_t src_id) {
  // LOOPBACK |Datain| Channel Config.
  if (LbCtrl::has_datain_channel_sel(version_)) {
    LbCtrl::Get()
        .ReadFrom(&view_)
        .set_datain_channel_nums(active_channels - 1)
        .set_datain_channel_mask(enable_mask)
        .WriteTo(&view_);
  } else {
    LbCtrl2::Get()
        .ReadFrom(&view_)
        .set_datain_channel_nums(active_channels - 1)
        .set_datain_channel_mask(enable_mask)
        .WriteTo(&view_);
  }

  // LOOPBACK |Datain| Source Config.
  if (LbCtrl::has_datain_src(version_)) {
    LbCtrl::Get().ReadFrom(&view_).set_datain_src(src_id).WriteTo(&view_);
  } else {
    LbCtrl2::Get().ReadFrom(&view_).set_datain_src(src_id).WriteTo(&view_);
  }

  // LOOPBACK |Datain| Packet.
  LbCtrl::Get()
      .ReadFrom(&view_)
      .set_packet_format(0)  // 32bits
      .set_msb(31)
      .set_lsb(0)
      .WriteTo(&view_);
  return ZX_OK;
}

zx_status_t AmlLoopbackDevice::ConfigDataLb(uint32_t active_channels, uint32_t enable_mask) {
  // LOOPBACK |Datalb| Channel Config.
  if (LbCtrl1::has_datalb_channel_sel(version_)) {
    LbCtrl1::Get()
        .ReadFrom(&view_)
        .set_datalb_channel_nums(active_channels - 1)
        .set_datalb_channel_mask(enable_mask)
        .WriteTo(&view_);
  } else {
    LbCtrl3::Get()
        .ReadFrom(&view_)
        .set_datalb_channel_nums(active_channels - 1)
        .set_datalb_channel_mask(enable_mask)
        .WriteTo(&view_);
  }

  // LOOPBACK |Datalb| Source Config.
  uint32_t src_id = {};
  if (LbCtrl1::has_datalb_src(version_)) {
    src_id = 0;  // 'TDMIN_LB' - S905D2/S905D3.
    LbCtrl1::Get().ReadFrom(&view_).set_datalb_src(src_id).WriteTo(&view_);
  } else {
    src_id = 6;  // 'TDMIN_LB' - A5.
    LbCtrl3::Get().ReadFrom(&view_).set_datalb_src(src_id).WriteTo(&view_);
  }

  // LOOPBACK |Datalb| Packet.
  LbCtrl1::Get()
      .ReadFrom(&view_)
      .set_packet_format(0)  // 32bits
      .set_msb(31)
      .set_lsb(0)
      .WriteTo(&view_);
  return ZX_OK;
}

}  // namespace audio::aml_g12

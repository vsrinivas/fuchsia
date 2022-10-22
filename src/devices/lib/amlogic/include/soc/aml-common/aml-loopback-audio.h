// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_LOOPBACK_AUDIO_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_LOOPBACK_AUDIO_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio-view.h>
#include <zircon/assert.h>

#include <soc/aml-common/aml-audio-regs.h>
#include <soc/aml-common/aml-audio.h>

namespace audio::aml_g12 {

class AmlLoopbackDevice {
 public:
  static std::unique_ptr<AmlLoopbackDevice> Create(const fdf::MmioBuffer& mmio,
                                                   const metadata::AmlVersion version,
                                                   const metadata::AmlLoopbackConfig config);

  static uint32_t ToLoopbackDatain(const metadata::AmlAudioBlock src) {
    switch (src) {
      case metadata::AmlAudioBlock::TDMIN_A:
        return 0;
      case metadata::AmlAudioBlock::TDMIN_B:
        return 1;
      case metadata::AmlAudioBlock::TDMIN_C:
        return 2;
      case metadata::AmlAudioBlock::PDMIN:
        return 4;
      case metadata::AmlAudioBlock::PDMIN_VAD:
        return 31;
      default:
        break;
    }
    ZX_ASSERT(0);
    return 0;
  }

  zx_status_t Initialize();

 protected:
  AmlLoopbackDevice(fdf::MmioView view, const metadata::AmlVersion version,
                    const metadata::AmlLoopbackConfig config)
      : view_(std::move(view)),
        version_(version),
        datain_src_(ToLoopbackDatain(config.datain_src)),
        datain_chnum_(config.datain_chnum),
        datain_chmask_(config.datain_chmask),
        datalb_chnum_(config.datalb_chnum),
        datalb_chmask_(config.datalb_chmask) {}

  // Config for LOOPBACK |datain|.
  zx_status_t ConfigDataIn(uint32_t active_channels, uint32_t enable_mask, uint32_t src_id);

  // Config for LOOPBACK |datalb|.
  zx_status_t ConfigDataLb(uint32_t active_channels, uint32_t enable_mask);

  // Config LOOPBACK's out rate mode
  void LbRateMode(bool is_lb_rate);

 private:
  const fdf::MmioView view_;
  const metadata::AmlVersion version_;
  const uint32_t datain_src_;
  const uint32_t datain_chnum_;
  const uint32_t datain_chmask_;
  const uint32_t datalb_chnum_;
  const uint32_t datalb_chmask_;
};

}  // namespace audio::aml_g12

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_LOOPBACK_AUDIO_H_

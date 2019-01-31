// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <utility>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <soc/mt8167/mt8167-audio-in.h>
#include <soc/mt8167/mt8167-audio-regs.h>

std::unique_ptr<MtAudioInDevice> MtAudioInDevice::Create(ddk::MmioBuffer mmio, MtI2sCh ch) {

    uint32_t fifo_depth = 0; // in bytes. TODO(andresoportus): Find out actual size.

    // TODO(andresoportus): Support other configurations.
    if (ch != I2S3) {
        return nullptr;
    }

    fbl::AllocChecker ac;
    auto dev = std::unique_ptr<MtAudioInDevice>(
        new (&ac) MtAudioInDevice(std::move(mmio), fifo_depth));
    if (!ac.check()) {
        return nullptr;
    }

    dev->InitRegs();
    return dev;
}

void MtAudioInDevice::InitRegs() {
    // Enable the AFE module.
    AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_AFE_ON(1).WriteTo(&mmio_);

    // Power up the AFE module by clearing the power down bit.
    AUDIO_TOP_CON0::Get().ReadFrom(&mmio_).set_PDN_AFE(0).WriteTo(&mmio_);

    // Route TDM_IN to afe_mem_if.
    AFE_CONN_TDMIN_CON::Get().FromValue(0).set_o_40_cfg(0).set_o_41_cfg(1).WriteTo(&mmio_);

    // Audio Interface.
    auto tdm_in = AFE_TDM_IN_CON1::Get().FromValue(0);
    tdm_in.set_tdm_en(1).set_tdm_fmt(1).set_tdm_lrck_inv(1);              // Enable, I2S, inv.
    tdm_in.set_tdm_wlen(1).set_LRCK_TDM_WIDTH(15);                        // 16 bits, 16 bits.
    tdm_in.set_fast_lrck_cycle_sel(0).set_tdm_channel(0).WriteTo(&mmio_); // LRCK 16 BCK, 2ch.
}

uint32_t MtAudioInDevice::GetRingPosition() {
    return AFE_HDMI_IN_2CH_CUR::Get().ReadFrom(&mmio_).reg_value() -
           AFE_HDMI_IN_2CH_BASE::Get().ReadFrom(&mmio_).reg_value();
}

zx_status_t MtAudioInDevice::SetBuffer(zx_paddr_t buf, size_t len) {
    if ((buf % 16) || ((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 16) ||
        (len % 16)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // End is inclusive.
    AFE_HDMI_IN_2CH_BASE::Get().FromValue(static_cast<uint32_t>(buf)).WriteTo(&mmio_);
    AFE_HDMI_IN_2CH_END::Get().FromValue(static_cast<uint32_t>(buf + len - 1)).WriteTo(&mmio_);
    return ZX_OK;
}

uint64_t MtAudioInDevice::Start() {
    AFE_HDMI_IN_2CH_CON0::Get().ReadFrom(&mmio_).set_AFE_HDMI_IN_2CH_OUT_ON(1).WriteTo(&mmio_);
    return 0;
}

void MtAudioInDevice::Stop() {
    AFE_HDMI_IN_2CH_CON0::Get().ReadFrom(&mmio_).set_AFE_HDMI_IN_2CH_OUT_ON(0).WriteTo(&mmio_);
}

void MtAudioInDevice::Shutdown() {
    Stop();
    // Disable the AFE module.
    // TODO(andresoportus): Manage multiple drivers accessing same registers, e.g. Audio In and Out.
    AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_AFE_ON(0).WriteTo(&mmio_);
}

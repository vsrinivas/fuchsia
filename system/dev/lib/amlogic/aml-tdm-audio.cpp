// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <limits>
#include <soc/aml-common/aml-tdm-audio.h>
#include <utility>

//static
fbl::unique_ptr<AmlTdmDevice> AmlTdmDevice::Create(ddk::MmioBuffer mmio,
                                                   ee_audio_mclk_src_t src,
                                                   aml_tdm_out_t tdm_dev,
                                                   aml_frddr_t frddr_dev,
                                                   aml_tdm_mclk_t mclk) {

    //A and B FRDDR have 128 lines in fifo, C has 256
    uint32_t fifo_depth = 128;
    if (frddr_dev == FRDDR_C) {
        fifo_depth = 256;
    }

    fbl::AllocChecker ac;
    auto tdm = fbl::unique_ptr<AmlTdmDevice>(new (&ac)
        AmlTdmDevice(std::move(mmio), src, tdm_dev, frddr_dev, mclk, fifo_depth));
    if (!ac.check()) {
        return nullptr;
    }

    tdm->InitRegs();

    return tdm;
}

void AmlTdmDevice::InitRegs() {
    //Enable the audio domain clocks used by this instance.
    AudioClkEna((EE_AUDIO_CLK_GATE_TDMOUTA << tdm_ch_) |
                (EE_AUDIO_CLK_GATE_FRDDRA << frddr_ch_) |
                EE_AUDIO_CLK_GATE_ARB);

    //Set chosen mclk channels input to selected source
    //Since this is init, set the divider to max value assuming it will
    //    be set to proper value later (slower is safer from circuit standpoint)
    //Leave disabled for now.
    zx_off_t ptr = EE_AUDIO_MCLK_A_CTRL + (mclk_ch_ * sizeof(uint32_t));
    mmio_.Write32((clk_src_ << 24) | 0xffff, ptr);

    //Set the sclk and lrclk sources to the chosen mclk channel
    ptr = EE_AUDIO_CLK_TDMOUT_A_CTL + tdm_ch_ * sizeof(uint32_t);
    mmio_.Write32((0x03 << 30) | (mclk_ch_ << 24) | (mclk_ch_ << 20), ptr);

    //Enable DDR ARB, and enable this ddr channels bit.
    mmio_.SetBits32((1 << 31) | (1 << (4 + frddr_ch_)), EE_AUDIO_ARB_CTRL);

    //Disable the FRDDR Channel
    //Only use one buffer
    //Interrupts off
    //ack delay = 0
    //set destination tdm block and enable that selection
    mmio_.Write32(tdm_ch_ | (1 << 3), GetFrddrOffset(FRDDR_CTRL0_OFFS));
    //use entire fifo, start transfer request when fifo is at 1/2 full
    //set the magic force end bit(12) to cause fetch from start
    //    -this only happens when the bit is set from 0->1 (edge)
    mmio_.Write32((1 << 12) | ((fifo_depth_ -1) << 24) | (((fifo_depth_ / 2) - 1) << 16),
        GetFrddrOffset(FRDDR_CTRL1_OFFS));

    //Value to be inserted in a slot if it is muted
    mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE_VAL_OFFS));
    //Value to be inserted in a slot if it is masked
    mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MASK_VAL_OFFS));
}

/* Notes
    -div is desired divider minus 1. (want /100? write 99)
*/
zx_status_t AmlTdmDevice::SetMclkDiv(uint32_t div) {
    //check that divider is in range
    ZX_DEBUG_ASSERT(div < (1 << kMclkDivBits));

    zx_off_t ptr = EE_AUDIO_MCLK_A_CTRL + (mclk_ch_ * sizeof(uint32_t));
    //disable and clear out old divider value
    mmio_.ClearBits32((1 << 31) | ((1 << kMclkDivBits) - 1), ptr);

    mmio_.SetBits32((1 << 31) | (div & ((1 << kMclkDivBits) - 1)), ptr);
    return ZX_OK;
}

uint32_t AmlTdmDevice::GetRingPosition() {
    return mmio_.Read32(GetFrddrOffset(FRDDR_STATUS2_OFFS)) -
           mmio_.Read32(GetFrddrOffset(FRDDR_START_ADDR_OFFS));
}
/* Notes:
    -sdiv is desired divider -1 (Want a divider of 10? write a value of 9)
    -sclk needs to be at least 2x mclk.  writing a value of 0 (/1) to sdiv
        will result in no sclk being generated on the sclk pin.  However, it
        appears that it is running properly as a lrclk is still generated at
        an expected rate (lrclk is derived from sclk)
*/
zx_status_t AmlTdmDevice::SetSclkDiv(uint32_t sdiv,
                                     uint32_t lrduty,
                                     uint32_t lrdiv) {
    ZX_DEBUG_ASSERT(sdiv < (1 << kSclkDivBits));
    ZX_DEBUG_ASSERT(lrdiv < (1 << kLRclkDivBits));
    //lrduty is in sclk cycles, so must be less than lrdiv
    ZX_DEBUG_ASSERT(lrduty < lrdiv);

    zx_off_t ptr = EE_AUDIO_MST_A_SCLK_CTRL0 + (2 * mclk_ch_ * sizeof(uint32_t));
    mmio_.Write32((0x3 << 30) |    //Enable the channel
                  (sdiv << 20) |   //sclk divider sclk=mclk/sdiv
                  (lrduty << 10) | //lrclk duty cycle in sclk cycles
                  (lrdiv << 0),    //lrclk = sclk/lrdiv
                  ptr);
    mmio_.Write32(0, ptr + sizeof(uint32_t)); //Clear delay lines for phases
    return ZX_OK;
}

void AmlTdmDevice::AudioClkEna(uint32_t audio_blk_mask) {
    mmio_.SetBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}

void AmlTdmDevice::AudioClkDis(uint32_t audio_blk_mask) {
    mmio_.ClearBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}

zx_status_t AmlTdmDevice::SetBuffer(zx_paddr_t buf, size_t len) {
    //Ensure ring buffer resides in lower memory (dma pointers are 32-bit)
    //    and len is at least 8 (size of each dma operation)
    if (((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 8)) {
        return ZX_ERR_INVALID_ARGS;
    }

    //Write32 the start and end pointers.  Each fetch is 64-bits, so end poitner
    //    is pointer to the last 64-bit fetch (inclusive)
    mmio_.Write32(static_cast<uint32_t>(buf), GetFrddrOffset(FRDDR_START_ADDR_OFFS));
    mmio_.Write32(static_cast<uint32_t>(buf + len - 8),
                GetFrddrOffset(FRDDR_FINISH_ADDR_OFFS));
    return ZX_OK;
}

/*
    bit_offset - bit position in frame where first slot will appear
                    (position 0 is concurrent with frame sync)
    num_slots - number of slots per frame minus one
    bits_per_slot - width of each slot minus one
    bits_per_sample - number of bits in sample minus one
*/
void AmlTdmDevice::ConfigTdmOutSlot(uint8_t bit_offset, uint8_t num_slots,
                                    uint8_t bits_per_slot, uint8_t bits_per_sample) {

    uint32_t reg = bits_per_slot | (num_slots << 5) | (bit_offset << 15);
    mmio_.Write32(reg, GetTdmOffset(TDMOUT_CTRL0_OFFS));

    reg = (bits_per_sample << 8) | (frddr_ch_ << 24);
    if (bits_per_sample <= 8) {
        // 8 bit sample, left justify in frame, split 64-bit dma fetch into 8 samples
        reg |= (0 << 4);
    } else if (bits_per_sample <= 16) {
        // 16 bit sample, left justify in frame, split 64-bit dma fetch into 2 samples
        reg |= (2 << 4);
    } else {
        // 32/24 bit sample, left justify in slot, split 64-bit dma fetch into 2 samples
        reg |= (4 << 4);
    }
    mmio_.Write32(reg, GetTdmOffset(TDMOUT_CTRL1_OFFS));

    // assign left ch to slot 1, right to slot 1
    mmio_.Write32(0x00000010, GetTdmOffset(TDMOUT_SWAP_OFFS));
    // unmask first two slots
    mmio_.Write32(0x00000003, GetTdmOffset(TDMOUT_MASK0_OFFS));
}

// Stops the tdm from clocking data out of fifo onto bus
void AmlTdmDevice::TdmOutDisable() {
    mmio_.ClearBits32(1 << 31, GetTdmOffset(TDMOUT_CTRL0_OFFS));
}
// Enables the tdm to clock data out of fifo onto bus
void AmlTdmDevice::TdmOutEnable() {
    mmio_.SetBits32(1 << 31, GetTdmOffset(TDMOUT_CTRL0_OFFS));
}

void AmlTdmDevice::FRDDREnable() {
    //Set the load bit, will make sure things start from beginning of buffer
    mmio_.SetBits32(1 << 12, GetFrddrOffset(FRDDR_CTRL1_OFFS));
    mmio_.SetBits32(1 << 31, GetFrddrOffset(FRDDR_CTRL0_OFFS));
}

void AmlTdmDevice::FRDDRDisable() {
    // Clear the load bit (this is the bit that forces the initial fetch of
    //    start address into current ptr)
    mmio_.ClearBits32(1 << 12, GetFrddrOffset(FRDDR_CTRL1_OFFS));
    // Disable the frddr channel
    mmio_.ClearBits32(1 << 31, GetFrddrOffset(FRDDR_CTRL0_OFFS));
}

void AmlTdmDevice::Sync() {
    mmio_.ClearBits32(3 << 28, GetTdmOffset(TDMOUT_CTRL0_OFFS));
    mmio_.SetBits32(1 << 29, GetTdmOffset(TDMOUT_CTRL0_OFFS));
    mmio_.SetBits32(1 << 28, GetTdmOffset(TDMOUT_CTRL0_OFFS));
}

// Resets frddr mechanisms to start at beginning of buffer
//   starts the frddr (this will fill the fifo)
//   starts the tdm to clock out data on the bus
// returns the start time
uint64_t AmlTdmDevice::Start() {
    uint64_t a, b;

    Sync();
    FRDDREnable();
    a = zx_clock_get(ZX_CLOCK_MONOTONIC);
    TdmOutEnable();
    b = zx_clock_get(ZX_CLOCK_MONOTONIC);
    return ((b - a) >> 1) + a;
}

void AmlTdmDevice::Stop() {
    TdmOutDisable();
    FRDDRDisable();
}

void AmlTdmDevice::Shutdown() {
    Stop();

    // Disable the output signals
    zx_off_t ptr = EE_AUDIO_CLK_TDMOUT_A_CTL + tdm_ch_ * sizeof(uint32_t);
    mmio_.ClearBits32(0x03 << 30, ptr);

    // Disable the audio domain clocks used by this instance.
    AudioClkDis((EE_AUDIO_CLK_GATE_TDMOUTA << tdm_ch_) |
                (EE_AUDIO_CLK_GATE_FRDDRA << frddr_ch_));

    //Note: We are leaving the ARB unit clocked as well as MCLK and
    //  SCLK generation units since it is possible they are used by
    //  some other audio driver outside of this instance
}

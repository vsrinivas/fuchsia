// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-pdm-audio.h>

// Filter configurations
// mode 1 lpf1
static const uint32_t lpf1m1[] = {
    0x000014, 0xffffb2, 0xfffed9, 0xfffdce, 0xfffd45, 0xfffe32, 0x000147, 0x000645, 0x000b86,
    0x000e21, 0x000ae3, 0x000000, 0xffeece, 0xffdca8, 0xffd212, 0xffd7d1, 0xfff2a7, 0x001f4c,
    0x0050c2, 0x0072aa, 0x006ff1, 0x003c32, 0xffdc4e, 0xff6a18, 0xff0fef, 0xfefbaf, 0xff4c40,
    0x000000, 0x00ebc8, 0x01c077, 0x02209e, 0x01c1a4, 0x008e60, 0xfebe52, 0xfcd690, 0xfb8fa5,
    0xfba498, 0xfd9812, 0x0181ce, 0x06f5f3, 0x0d112f, 0x12a958, 0x169686, 0x18000e, 0x169686,
    0x12a958, 0x0d112f, 0x06f5f3, 0x0181ce, 0xfd9812, 0xfba498, 0xfb8fa5, 0xfcd690, 0xfebe52,
    0x008e60, 0x01c1a4, 0x02209e, 0x01c077, 0x00ebc8, 0x000000, 0xff4c40, 0xfefbaf, 0xff0fef,
    0xff6a18, 0xffdc4e, 0x003c32, 0x006ff1, 0x0072aa, 0x0050c2, 0x001f4c, 0xfff2a7, 0xffd7d1,
    0xffd212, 0xffdca8, 0xffeece, 0x000000, 0x000ae3, 0x000e21, 0x000b86, 0x000645, 0x000147,
    0xfffe32, 0xfffd45, 0xfffdce, 0xfffed9, 0xffffb2, 0x000014,
};
constexpr uint32_t kLpf1m1Len = static_cast<uint32_t>(countof(lpf1m1));

// mode 1 lpf3
static const uint32_t lpf3m1[] = {
    0x000000, 0x000081, 0x000000, 0xfffedb, 0x000000, 0x00022d, 0x000000, 0xfffc46, 0x000000,
    0x0005f7, 0x000000, 0xfff6eb, 0x000000, 0x000d4e, 0x000000, 0xffed1e, 0x000000, 0x001a1c,
    0x000000, 0xffdcb0, 0x000000, 0x002ede, 0x000000, 0xffc2d1, 0x000000, 0x004ebe, 0x000000,
    0xff9beb, 0x000000, 0x007dd7, 0x000000, 0xff633a, 0x000000, 0x00c1d2, 0x000000, 0xff11d5,
    0x000000, 0x012368, 0x000000, 0xfe9c45, 0x000000, 0x01b252, 0x000000, 0xfdebf6, 0x000000,
    0x0290b8, 0x000000, 0xfcca0d, 0x000000, 0x041d7c, 0x000000, 0xfa8152, 0x000000, 0x07e9c6,
    0x000000, 0xf28fb5, 0x000000, 0x28b216, 0x3fffde, 0x28b216, 0x000000, 0xf28fb5, 0x000000,
    0x07e9c6, 0x000000, 0xfa8152, 0x000000, 0x041d7c, 0x000000, 0xfcca0d, 0x000000, 0x0290b8,
    0x000000, 0xfdebf6, 0x000000, 0x01b252, 0x000000, 0xfe9c45, 0x000000, 0x012368, 0x000000,
    0xff11d5, 0x000000, 0x00c1d2, 0x000000, 0xff633a, 0x000000, 0x007dd7, 0x000000, 0xff9beb,
    0x000000, 0x004ebe, 0x000000, 0xffc2d1, 0x000000, 0x002ede, 0x000000, 0xffdcb0, 0x000000,
    0x001a1c, 0x000000, 0xffed1e, 0x000000, 0x000d4e, 0x000000, 0xfff6eb, 0x000000, 0x0005f7,
    0x000000, 0xfffc46, 0x000000, 0x00022d, 0x000000, 0xfffedb, 0x000000, 0x000081, 0x000000,
};
constexpr uint32_t kLpf3m1Len = static_cast<uint32_t>(countof(lpf3m1));

// osr64 lpf2
static const uint32_t lpf2osr64[] = {
    0x00050a, 0xfff004, 0x0002c1, 0x003c12, 0xffa818, 0xffc87d, 0x010aef, 0xff5223, 0xfebd93,
    0x028f41, 0xff5c0e, 0xfc63f8, 0x055f81, 0x000000, 0xf478a0, 0x11c5e3, 0x2ea74d, 0x11c5e3,
    0xf478a0, 0x000000, 0x055f81, 0xfc63f8, 0xff5c0e, 0x028f41, 0xfebd93, 0xff5223, 0x010aef,
    0xffc87d, 0xffa818, 0x003c12, 0x0002c1, 0xfff004, 0x00050a,
};
constexpr uint32_t kLpf2osr64Len = static_cast<uint32_t>(countof(lpf2osr64));

// static
std::unique_ptr<AmlPdmDevice> AmlPdmDevice::Create(ddk::MmioBuffer pdm_mmio,
                                                   ddk::MmioBuffer audio_mmio,
                                                   ee_audio_mclk_src_t pdm_clk_src,
                                                   uint32_t sysclk_div, uint32_t dclk_div,
                                                   aml_toddr_t toddr_dev, AmlVersion version) {
  // TODDR A has 256 64-bit lines in the FIFO, B and C have 128.
  uint32_t fifo_depth = 128 * 8;  // in bytes.
  if (toddr_dev == TODDR_A) {
    fifo_depth = 256 * 8;
  }

  fbl::AllocChecker ac;
  auto pdm = std::unique_ptr<AmlPdmDevice>(
      new (&ac) AmlPdmDevice(std::move(pdm_mmio), std::move(audio_mmio), pdm_clk_src, sysclk_div,
                             dclk_div, toddr_dev, fifo_depth, version));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Could not create AmlPdmDevice", __func__);
    return nullptr;
  }

  pdm->InitRegs();
  constexpr uint32_t default_frames_per_second = 48000;
  pdm->ConfigFilters(default_frames_per_second);

  return pdm;
}

void AmlPdmDevice::InitRegs() {
  // Setup toddr block
  switch (version_) {
    case AmlVersion::kS905D2G:
      audio_mmio_.Write32((0x02 << 13) |    // Right justified 16-bit
                              (31 << 8) |   // msb position of data out of pdm
                              (16 << 3) |   // lsb position of data out of pdm
                              (0x04 << 0),  // select pdm as data source
                          GetToddrOffset(TODDR_CTRL0_OFFS));
      audio_mmio_.Write32(((fifo_depth_ / 8 / 2) << 16) |  // trigger ddr when fifo half full
                              (0x02 << 8),                 // STATUS2 source is ddr position
                          GetToddrOffset(TODDR_CTRL1_OFFS));
      break;
    case AmlVersion::kS905D3G:
      audio_mmio_.Write32((0x02 << 13) |   // Right justified 16-bit
                              (31 << 8) |  // msb position of data out of pdm
                              (16 << 3),   // lsb position of data out of pdm
                          GetToddrOffset(TODDR_CTRL0_OFFS));
      audio_mmio_.Write32((0x04 << 28) |                       // select pdm as data source
                              ((fifo_depth_ / 8 / 2) << 12) |  // trigger ddr when fifo half full
                              (0x02 << 8),                     // STATUS2 source is ddr position
                          GetToddrOffset(TODDR_CTRL1_OFFS));
      break;
  }

  //*To keep things simple, we are using the same clock source for both the
  // pdm sysclk and dclk.  Sysclk needs to be ~100-200MHz per AmLogic recommendations.
  // dclk is osr*fs
  //*Sysclk must be configured, enabled, and PDM audio clock gated prior to
  // accessing any of the registers mapped via pdm_mmio.  Writing without sysclk
  // operating properly (and in range) will result in unknown results, reads
  // will wedge the system.
  audio_mmio_.Write32((clk_src_ << 24) | dclk_div_, EE_AUDIO_CLK_PDMIN_CTRL0);
  audio_mmio_.Write32((1 << 31) | (clk_src_ << 24) | sysclk_div_, EE_AUDIO_CLK_PDMIN_CTRL1);

  audio_mmio_.SetBits32((1 << 31) | (1 << toddr_ch_), EE_AUDIO_ARB_CTRL);

  // Enable the audio domain clocks used by this instance.
  AudioClkEna(EE_AUDIO_CLK_GATE_PDM | (EE_AUDIO_CLK_GATE_TODDRA << toddr_ch_) |
              EE_AUDIO_CLK_GATE_ARB);
  // It is now safe to write to pdm registers

  // Ensure clocks are stable before accessing any of the pdm_mmio_ registers.
  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  // Ensure system is in idle state in case we are re-initing hardware
  // which was already running.  Keep de-inited for 100ms with no pdm_dclk to
  // ensure pdm microphones will start reliably.
  Stop();
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  // Enable cts_pdm_clk gate (clock gate within pdm module)
  pdm_mmio_.SetBits32(0x01, PDM_CLKG_CTRL);

  pdm_mmio_.Write32((0x01 << 29),  // 24bit output mode
                    PDM_CTRL);

  // This sets the number of sysclk cycles between edge of dclk and when
  // data is sampled.  AmLogic material suggests this should be 3/4 of a
  // dclk half-cycle.  Go ahead and set all eight channels.
  uint32_t samp_delay = 3 * (dclk_div_ + 1) / (4 * 2 * (sysclk_div_ + 1));
  pdm_mmio_.Write32((samp_delay << 0) | (samp_delay << 8) | (samp_delay << 16) | (samp_delay << 24),
                    PDM_CHAN_CTRL);
  pdm_mmio_.Write32((samp_delay << 0) | (samp_delay << 8) | (samp_delay << 16) | (samp_delay << 24),
                    PDM_CHAN_CTRL1);
}

void AmlPdmDevice::ConfigFilters(uint32_t frames_per_second) {
  ZX_ASSERT(frames_per_second == 96000 || frames_per_second == 48000);

  uint32_t gain_shift = (frames_per_second == 96000) ? 0xe : 0x15;
  uint32_t downsample_rate = (frames_per_second == 96000) ? 0x4 : 0x8;

  pdm_mmio_.Write32((1 << 31) |                   // Enable
                        (gain_shift << 24) |      // Final gain shift parameter
                        (0x80 << 16) |            // Final gain multiplier
                        (downsample_rate << 4) |  // hcic downsample rate
                        (0x07 << 0),              // hcic stage number (must be between 3-9)
                    PDM_HCIC_CTRL1);

  // Note: The round mode field for the lowpass control registers is shown in AmLogic
  // documentation to be occupying bits [16:15] fo the register.  This was confirmed
  // by amlogic to be an error in the datasheet and the correct position is [17:16]
  pdm_mmio_.Write32((0x01 << 31) |          // Enable filter
                        (0x01 << 16) |      // Round mode
                        (0x02 << 12) |      // Filter 1 downsample rate
                        (kLpf1m1Len << 0),  // Number of taps in filter
                    PDM_F1_CTRL);
  pdm_mmio_.Write32((0x01 << 31) |             // Enable filter
                        (0x00 << 16) |         // Round mode
                        (0x02 << 12) |         // Filter 2 downsample rate
                        (kLpf2osr64Len << 0),  // Number of taps in filter
                    PDM_F2_CTRL);

  pdm_mmio_.Write32((0x01 << 31) |          // Enable filter
                        (0x01 << 16) |      // Round mode
                        (2 << 12) |         // Filter 3 downsample rate
                        (kLpf3m1Len << 0),  // Number of taps in filter
                    PDM_F3_CTRL);
  pdm_mmio_.Write32((0x01 << 31) |      // Enable filter
                        (0x0d << 16) |  // Shift steps
                        (0x8000 << 0),  // Output factor
                    PDM_HPF_CTRL);

  // set coefficient index pointer to 0
  pdm_mmio_.Write32(0x0000, PDM_COEFF_ADDR);

  // Write coefficients to coefficient memory
  //  --these appear to be packed with the filter length in each filter
  //    control register being the mechanism that helps reference them
  for (uint32_t i = 0; i < countof(lpf1m1); i++) {
    pdm_mmio_.Write32(lpf1m1[i], PDM_COEFF_DATA);
  }
  for (uint32_t i = 0; i < countof(lpf2osr64); i++) {
    pdm_mmio_.Write32(lpf2osr64[i], PDM_COEFF_DATA);
  }
  for (uint32_t i = 0; i < countof(lpf3m1); i++) {
    pdm_mmio_.Write32(lpf3m1[i], PDM_COEFF_DATA);
  }

  // set coefficient index pointer back to 0
  pdm_mmio_.Write32(0x0000, PDM_COEFF_ADDR);
}

zx_status_t AmlPdmDevice::SetRate(uint32_t frames_per_second) {
  if (frames_per_second != 48000 && frames_per_second != 96000) {
    return ZX_ERR_INVALID_ARGS;
  }
  ConfigFilters(frames_per_second);
  return ZX_OK;
}

uint32_t AmlPdmDevice::GetRingPosition() {
  uint32_t pos = audio_mmio_.Read32(GetToddrOffset(TODDR_STATUS2_OFFS));
  uint32_t base = audio_mmio_.Read32(GetToddrOffset(TODDR_START_ADDR_OFFS));
  return (pos - base);
}

void AmlPdmDevice::AudioClkEna(uint32_t audio_blk_mask) {
  audio_mmio_.SetBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}

void AmlPdmDevice::AudioClkDis(uint32_t audio_blk_mask) {
  audio_mmio_.ClearBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}

zx_status_t AmlPdmDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  // Ensure ring buffer resides in lower memory (dma pointers are 32-bit)
  //    and len is at least 8 (size of each dma operation)
  if (((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 8)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Write32 the start and end pointers.  Each fetch is 64-bits, so end pointer
  //    is pointer to the last 64-bit fetch (inclusive)
  audio_mmio_.Write32(static_cast<uint32_t>(buf), GetToddrOffset(TODDR_START_ADDR_OFFS));
  audio_mmio_.Write32(static_cast<uint32_t>(buf), GetToddrOffset(TODDR_INIT_ADDR_OFFS));
  audio_mmio_.Write32(static_cast<uint32_t>(buf + len - 8), GetToddrOffset(TODDR_FINISH_ADDR_OFFS));
  return ZX_OK;
}

// Stops the pdm from clocking
void AmlPdmDevice::PdmInDisable() {
  audio_mmio_.ClearBits32(1 << 31, EE_AUDIO_CLK_PDMIN_CTRL0);
  pdm_mmio_.ClearBits32((1 << 31) | (1 << 16), PDM_CTRL);
}

// Enables the pdm to clock data
void AmlPdmDevice::PdmInEnable() {
  // Start pdm_dclk
  audio_mmio_.SetBits32(1 << 31, EE_AUDIO_CLK_PDMIN_CTRL0);
  pdm_mmio_.SetBits32((1 << 31) | (1 << 16), PDM_CTRL);
}

// Takes channels out of reset and enables them.
void AmlPdmDevice::ConfigPdmIn(uint8_t mask) {
  pdm_mmio_.ModifyBits<uint32_t>((mask << 8) | (mask << 0), (0xff << 8) | (0xff << 0), PDM_CTRL);
}

void AmlPdmDevice::TODDREnable() {
  // Set the load bit, will make sure things start from beginning of buffer
  audio_mmio_.SetBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS));
}

void AmlPdmDevice::TODDRDisable() {
  // Clear the load bit (this is the bit that forces the initial fetch of
  //    start address into current ptr)
  audio_mmio_.ClearBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS));
  audio_mmio_.ClearBits32(1 << 25, GetToddrOffset(TODDR_CTRL1_OFFS));
}

void AmlPdmDevice::Sync() {
  pdm_mmio_.ClearBits32(1 << 16, PDM_CTRL);
  pdm_mmio_.SetBits32(1 << 16, PDM_CTRL);
}

// Resets frddr mechanisms to start at beginning of buffer
//   starts the frddr (this will fill the fifo)
//   starts the tdm to clock out data on the bus
// returns the start time
uint64_t AmlPdmDevice::Start() {
  uint64_t a, b;

  Sync();
  TODDREnable();
  a = zx_clock_get_monotonic();
  PdmInEnable();
  b = zx_clock_get_monotonic();
  return ((b - a) >> 1) + a;
}

void AmlPdmDevice::Stop() {
  PdmInDisable();
  TODDRDisable();
}

void AmlPdmDevice::Shutdown() { Stop(); }

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_TDM_AUDIO_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_TDM_AUDIO_H_

#include <assert.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <utility>

#include <fbl/macros.h>
#include <soc/aml-common/aml-audio-regs.h>
#include <soc/aml-common/aml-audio.h>

class AmlTdmDevice {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AmlTdmDevice);

  static std::unique_ptr<AmlTdmDevice> Create(
      fdf::MmioBuffer mmio, ee_audio_mclk_src_t src, aml_tdm_out_t tdm, aml_frddr_t frddr,
      aml_tdm_mclk_t mclk, metadata::AmlVersion version = metadata::AmlVersion::kS905D2G);

  // Configure an mclk channel divider
  zx_status_t SetMclkDiv(uint32_t div);
  // Configure an sclk/lclk generator block
  zx_status_t SetSclkDiv(uint32_t sdiv, uint32_t lrduty, uint32_t lrdiv, bool sclk_invert_ph0);
  // Configures the mclk pad.
  zx_status_t SetMClkPad(aml_tdm_mclk_pad_t mclk_pad);

  // Configures placement of data on the tdm bus
  virtual void ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                             uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) = 0;

  // Configures Lanes.
  virtual zx_status_t ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) = 0;

  // Configures TDM swaps.
  virtual void ConfigTdmSwaps(uint32_t swaps) = 0;

  // Sets the buffer/length pointers for dma engine
  // must resize in lower 32-bits of address space.
  virtual zx_status_t SetBuffer(zx_paddr_t buf, size_t len) = 0;

  // Get HW alignment required in the buffer in bytes.
  static uint32_t GetBufferAlignment() { return 8; }

  // Returns offset of dma pointer in the ring buffer.
  virtual uint32_t GetRingPosition() = 0;

  // Returns DMA status bits.
  virtual uint32_t GetDmaStatus() = 0;

  // Returns TDM status bits.
  virtual uint32_t GetTdmStatus() = 0;

  // Resets state of dma mechanisms and starts clocking data
  // onto/from tdm bus with data fetched from beginning of buffer.
  virtual uint64_t Start() = 0;

  // Stops clocking data out/in on/from the TDM bus (physical tdm bus signals remain active).
  virtual void Stop() = 0;

  // Synchronize the state of TDM bus signals with fifo/dma engine.
  virtual void Sync() = 0;

  // Start clocking, configure the DDR and TDM interfaces.
  virtual void Initialize() = 0;

  // Stops the clocking data, shuts down the DDR interface, and quiets output signals.
  virtual void Shutdown() = 0;

  virtual uint32_t fifo_depth() const = 0;

  virtual const fdf::MmioBuffer& GetMmio() const = 0;

 protected:
  friend class std::default_delete<AmlTdmDevice>;

  AmlTdmDevice(aml_tdm_mclk_t mclk_ch, const ee_audio_mclk_src_t clk_src,
               const metadata::AmlVersion version)
      : mclk_ch_(mclk_ch), clk_src_(clk_src), version_(version) {}
  virtual ~AmlTdmDevice() = default;

  void InitMclk();
  void InitMstPad();
  void AudioClkEna(uint32_t audio_blk_mask);
  void AudioClkDis(uint32_t audio_blk_mask);

 private:
  static constexpr int32_t kMclkDivBits = 16;
  static constexpr int32_t kSclkDivBits = 10;
  static constexpr int32_t kLRclkDivBits = 10;

  const aml_tdm_mclk_t mclk_ch_;  // mclk channel used by this instance
  const ee_audio_mclk_src_t clk_src_;
  const metadata::AmlVersion version_;
};

class AmlTdmOutDevice : public AmlTdmDevice {  // Not final for unit testing.
 public:
  static std::unique_ptr<AmlTdmDevice> Create(
      fdf::MmioBuffer mmio, ee_audio_mclk_src_t src, aml_tdm_out_t tdm, aml_frddr_t frddr,
      aml_tdm_mclk_t mclk, metadata::AmlVersion version = metadata::AmlVersion::kS905D2G);

  void ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                     uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) override;
  zx_status_t ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) override;
  void ConfigTdmSwaps(uint32_t swaps) override;
  zx_status_t SetBuffer(zx_paddr_t buf, size_t len) override;
  uint32_t GetRingPosition() override;
  uint32_t GetDmaStatus() override;
  uint32_t GetTdmStatus() override;
  uint64_t Start() override;
  void Stop() override;
  void Sync() override;
  void Initialize() override;
  void Shutdown() override;
  uint32_t fifo_depth() const override { return fifo_depth_; }

 protected:
  AmlTdmOutDevice(fdf::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_out_t tdm,
                  aml_frddr_t frddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                  metadata::AmlVersion version)
      : AmlTdmDevice(mclk, clk_src, version),
        fifo_depth_(fifo_depth),
        tdm_ch_(tdm),
        frddr_ch_(frddr),
        mclk_ch_(mclk),
        frddr_base_(GetFrddrBase(frddr)),
        tdm_base_(GetTdmBase(tdm)),
        mmio_(std::move(mmio)),
        version_(version) {}

 private:
  const fdf::MmioBuffer& GetMmio() const override { return mmio_; }

  /* Get the register block offset for our ddr block */
  zx_off_t GetFrddrBase(aml_frddr_t ch) {
    switch (ch) {
      case FRDDR_A:
        return EE_AUDIO_FRDDR_A_CTRL0;
      case FRDDR_B:
        return EE_AUDIO_FRDDR_B_CTRL0;
      case FRDDR_C:
        return EE_AUDIO_FRDDR_C_CTRL0;
    }
    // We should never get here, but if we do, make it obvious
    assert(0);
    return 0;
  }
  /* Get the register block offset for our tdm block */
  zx_off_t GetTdmBase(aml_tdm_out_t ch) {
    switch (ch) {
      case TDM_OUT_A:
        return EE_AUDIO_TDMOUT_A_CTRL0;
      case TDM_OUT_B:
        return EE_AUDIO_TDMOUT_B_CTRL0;
      case TDM_OUT_C:
        return EE_AUDIO_TDMOUT_C_CTRL0;
    }
    // We should never get here, but if we do, make it obvious
    assert(0);
    return 0;
  }

  void FRDDREnable();
  void FRDDRDisable();
  void TdmOutDisable();
  void TdmOutEnable();

  /* Get the register block offset for our ddr block */
  zx_off_t GetFrddrOffset(zx_off_t off) { return frddr_base_ + off; }
  /* Get the register block offset for our tdm block */
  zx_off_t GetTdmOffset(zx_off_t off) { return tdm_base_ + off; }

  const uint32_t fifo_depth_;     // in bytes.
  const aml_tdm_out_t tdm_ch_;    // tdm output block used by this instance
  const aml_frddr_t frddr_ch_;    // fromddr channel used by this instance
  const aml_tdm_mclk_t mclk_ch_;  // mclk channel used by this instance
  const zx_off_t frddr_base_;     // base offset of frddr ch used by this instance
  const zx_off_t tdm_base_;       // base offset of our tdmout block
  const fdf::MmioBuffer mmio_;
  const metadata::AmlVersion version_;
};

class AmlTdmInDevice : public AmlTdmDevice {  // Not final for unit testing.
 public:
  static std::unique_ptr<AmlTdmDevice> Create(
      fdf::MmioBuffer mmio, ee_audio_mclk_src_t src, aml_tdm_in_t tdm, aml_toddr_t toddr,
      aml_tdm_mclk_t mclk, metadata::AmlVersion version = metadata::AmlVersion::kS905D2G);

  void ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                     uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) override;
  zx_status_t ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) override;
  void ConfigTdmSwaps(uint32_t swaps) override;
  zx_status_t SetBuffer(zx_paddr_t buf, size_t len) override;
  uint32_t GetRingPosition() override;
  uint32_t GetDmaStatus() override;
  uint32_t GetTdmStatus() override;
  uint64_t Start() override;
  void Stop() override;
  void Sync() override;
  void Initialize() override;
  void Shutdown() override;
  uint32_t fifo_depth() const override { return fifo_depth_; }

 protected:
  AmlTdmInDevice(fdf::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_in_t tdm,
                 aml_toddr_t toddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                 metadata::AmlVersion version)
      : AmlTdmDevice(mclk, clk_src, version),
        fifo_depth_(fifo_depth),
        tdm_ch_(tdm),
        toddr_ch_(toddr),
        mclk_ch_(mclk),
        toddr_base_(GetToddrBase(toddr)),
        tdm_base_(GetTdmBase(tdm)),
        mmio_(std::move(mmio)),
        version_(version) {}

 private:
  const fdf::MmioBuffer& GetMmio() const override { return mmio_; }

  /* Get the register block offset for our ddr block */
  static zx_off_t GetToddrBase(aml_toddr_t ch) {
    switch (ch) {
      case TODDR_A:
        return EE_AUDIO_TODDR_A_CTRL0;
      case TODDR_B:
        return EE_AUDIO_TODDR_B_CTRL0;
      case TODDR_C:
        return EE_AUDIO_TODDR_C_CTRL0;
    }
    // We should never get here, but if we do, make it obvious
    assert(0);
    return 0;
  }
  /* Get the register block offset for our tdm block */
  static zx_off_t GetTdmBase(aml_tdm_in_t ch) {
    switch (ch) {
      case TDM_IN_A:
        return EE_AUDIO_TDMIN_A_CTRL0;
      case TDM_IN_B:
        return EE_AUDIO_TDMIN_B_CTRL0;
      case TDM_IN_C:
        return EE_AUDIO_TDMIN_C_CTRL0;
    }
    // We should never get here, but if we do, make it obvious
    assert(0);
    return 0;
  }

  void TODDREnable();
  void TODDRDisable();
  void TdmInDisable();
  void TdmInEnable();

  /* Get the register block offset for our ddr block */
  zx_off_t GetToddrOffset(zx_off_t off) { return toddr_base_ + off; }
  /* Get the register block offset for our tdm block */
  zx_off_t GetTdmOffset(zx_off_t off) { return tdm_base_ + off; }

  const uint32_t fifo_depth_;     // in bytes.
  const aml_tdm_in_t tdm_ch_;     // tdm input block used by this instance
  const aml_toddr_t toddr_ch_;    // fromddr channel used by this instance
  const aml_tdm_mclk_t mclk_ch_;  // mclk channel used by this instance
  const zx_off_t toddr_base_;     // base offset of toddr ch used by this instance
  const zx_off_t tdm_base_;       // base offset of our tdmin block
  const fdf::MmioBuffer mmio_;
  const metadata::AmlVersion version_;
};

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_TDM_AUDIO_H_

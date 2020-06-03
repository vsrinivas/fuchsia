// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_MT8167_I2C_MT8167_I2C_REGS_H_
#define SRC_DEVICES_I2C_DRIVERS_MT8167_I2C_MT8167_I2C_REGS_H_

#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>

namespace mt8167_i2c {

class DataPortReg : public hwreg::RegisterBase<DataPortReg, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DataPortReg>(0x00); }
};

class SlaveAddrReg : public hwreg::RegisterBase<SlaveAddrReg, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SlaveAddrReg>(0x04); }
};

class IntrMaskReg : public hwreg::RegisterBase<IntrMaskReg, uint32_t> {
 public:
  DEF_BIT(4, rs_multiple);
  DEF_BIT(3, mas_arb_lost);
  DEF_BIT(2, mas_hs_nacker);
  DEF_BIT(1, mas_ackerr);
  DEF_BIT(0, mas_transac_comp);
  static auto Get() { return hwreg::RegisterAddr<IntrMaskReg>(0x08); }
};

class IntrStatReg : public hwreg::RegisterBase<IntrStatReg, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(4, rs_multiple);
  DEF_BIT(3, arb_lost);
  DEF_BIT(2, hs_nacker);
  DEF_BIT(1, ackerr);
  DEF_BIT(0, transac_comp);
  static auto Get() { return hwreg::RegisterAddr<IntrStatReg>(0x0c); }
};

class ControlReg : public hwreg::RegisterBase<ControlReg, uint32_t> {
 public:
  DEF_BIT(6, transfer_len_change);
  DEF_BIT(5, ackerr_det_en);
  DEF_BIT(4, dir_change);
  DEF_BIT(3, clk_ext_en);
  DEF_BIT(2, dma_en);
  DEF_BIT(1, rs_stop);
  static auto Get() { return hwreg::RegisterAddr<ControlReg>(0x10); }
};

// This register is not documented in the datasheet.
class TransferLenReg : public hwreg::RegisterBase<TransferLenReg, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TransferLenReg>(0x14); }
};

class TransacLenReg : public hwreg::RegisterBase<TransacLenReg, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TransacLenReg>(0x18); }
};

class DelayLenReg : public hwreg::RegisterBase<DelayLenReg, uint8_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DelayLenReg>(0x1c); }
};

class TimingReg : public hwreg::RegisterBase<TimingReg, uint32_t> {
 public:
  DEF_BIT(15, data_read_adj);
  DEF_FIELD(14, 12, data_read_time);
  DEF_FIELD(10, 8, sample_cnt_div);
  DEF_FIELD(5, 0, step_cnt_div);
  static auto Get() { return hwreg::RegisterAddr<TimingReg>(0x20); }
};

class StartReg : public hwreg::RegisterBase<StartReg, uint32_t> {
 public:
  DEF_BIT(15, rs_stop_multiple_config);
  DEF_BIT(14, rs_stop_multiple_trig);
  DEF_BIT(13, rs_stop_multiple_trig_clr);
  DEF_BIT(0, start);
  static auto Get() { return hwreg::RegisterAddr<StartReg>(0x24); }
};

class ExtConfReg : public hwreg::RegisterBase<ExtConfReg, uint32_t> {
 public:
  DEF_FIELD(15, 8, ext_time);
  DEF_BIT(0, ext_en);
  static auto Get() { return hwreg::RegisterAddr<ExtConfReg>(0x28); }
};

class FifoStatReg : public hwreg::RegisterBase<FifoStatReg, uint32_t> {
 public:
  DEF_FIELD(15, 12, rd_addr);
  DEF_FIELD(11, 8, wr_addr);
  DEF_FIELD(7, 4, fifo_offset);
  DEF_BIT(1, wr_full);
  DEF_BIT(0, rd_empty);
  static auto Get() { return hwreg::RegisterAddr<FifoStatReg>(0x30); }
};

class FifoThreshReg : public hwreg::RegisterBase<FifoThreshReg, uint32_t> {
 public:
  DEF_FIELD(10, 8, tx_trig_thresh);
  DEF_FIELD(2, 0, rx_trig_thresh);
  static auto Get() { return hwreg::RegisterAddr<FifoThreshReg>(0x34); }
};

class FifoAddrClrReg : public hwreg::RegisterBase<FifoAddrClrReg, uint32_t> {
 public:
  DEF_BIT(0, fifo_addr_clr);
  static auto Get() { return hwreg::RegisterAddr<FifoAddrClrReg>(0x38); }
};

class IoConfigReg : public hwreg::RegisterBase<IoConfigReg, uint32_t> {
 public:
  DEF_BIT(3, idle_oe_en);
  DEF_BIT(2, io_sync_en);
  DEF_BIT(1, sda_io_config);
  DEF_BIT(0, scl_io_config);
  static auto Get() { return hwreg::RegisterAddr<IoConfigReg>(0x40); }
};

class DebugReg : public hwreg::RegisterBase<DebugReg, uint32_t> {
 public:
  DEF_FIELD(2, 0, debug);
  static auto Get() { return hwreg::RegisterAddr<DebugReg>(0x44); }
};

class HsReg : public hwreg::RegisterBase<HsReg, uint32_t> {
 public:
  DEF_FIELD(14, 12, hs_sample_cnt_div);
  DEF_FIELD(10, 8, hs_step_cnt_div);
  DEF_FIELD(6, 4, master_code);
  DEF_BIT(1, hs_nackerr_det_en);
  DEF_BIT(0, hs_en);
  static auto Get() { return hwreg::RegisterAddr<HsReg>(0x48); }
};

class SoftResetReg : public hwreg::RegisterBase<SoftResetReg, uint32_t> {
 public:
  DEF_BIT(0, soft_reset);
  static auto Get() { return hwreg::RegisterAddr<SoftResetReg>(0x50); }
};

class HwDcmEnableReg : public hwreg::RegisterBase<HwDcmEnableReg, uint32_t> {
 public:
  DEF_BIT(0, dcm_en);
  static auto Get() { return hwreg::RegisterAddr<HwDcmEnableReg>(0x54); }
};

class DebugStatReg : public hwreg::RegisterBase<DebugStatReg, uint32_t> {
 public:
  DEF_BIT(7, bus_busy);
  DEF_BIT(6, master_write);
  DEF_BIT(5, master_read);
  DEF_FIELD(4, 0, master_state);
  static auto Get() { return hwreg::RegisterAddr<DebugStatReg>(0x64); }
};

class DebugCtrlReg : public hwreg::RegisterBase<DebugCtrlReg, uint32_t> {
 public:
  DEF_BIT(2, bypass_master_sync_en);
  DEF_BIT(1, apb_debug_rd);
  DEF_BIT(0, fifo_apb_debug);
  static auto Get() { return hwreg::RegisterAddr<DebugCtrlReg>(0x68); }
};

class TransferLenAuxReg : public hwreg::RegisterBase<TransferLenAuxReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, transfer_len_aux);
  static auto Get() { return hwreg::RegisterAddr<TransferLenAuxReg>(0x6c); }
};

class ClockDivReg : public hwreg::RegisterBase<ClockDivReg, uint32_t> {
 public:
  DEF_FIELD(2, 0, clock_div);
  static auto Get() { return hwreg::RegisterAddr<ClockDivReg>(0x70); }
};

class SclHighLowRatioReg : public hwreg::RegisterBase<SclHighLowRatioReg, uint32_t> {
 public:
  DEF_BIT(12, scl_high_low_ratio_en);
  DEF_FIELD(11, 6, step_high_cnt_div);
  DEF_FIELD(5, 0, step_low_cnt_div);
  static auto Get() { return hwreg::RegisterAddr<SclHighLowRatioReg>(0x74); }
};

class HsSclHighLowRatioReg : public hwreg::RegisterBase<HsSclHighLowRatioReg, uint32_t> {
 public:
  DEF_BIT(12, hs_scl_high_low_ratio_en);
  DEF_FIELD(11, 6, hs_step_high_cnt_div);
  DEF_FIELD(5, 0, hs_step_low_cnt_div);
  static auto Get() { return hwreg::RegisterAddr<HsSclHighLowRatioReg>(0x78); }
};

class SclMisCompPointReg : public hwreg::RegisterBase<SclMisCompPointReg, uint32_t> {
 public:
  DEF_FIELD(5, 0, scl_mis_comp_point);
  static auto Get() { return hwreg::RegisterAddr<SclMisCompPointReg>(0x7c); }
};

class StaStopAcTimingReg : public hwreg::RegisterBase<StaStopAcTimingReg, uint32_t> {
 public:
  DEF_FIELD(13, 8, step_stop_cnt_dev);
  DEF_FIELD(5, 0, step_start_cnt_dev);
  static auto Get() { return hwreg::RegisterAddr<StaStopAcTimingReg>(0x80); }
};

class HsStaStopAcTimingReg : public hwreg::RegisterBase<HsStaStopAcTimingReg, uint32_t> {
 public:
  DEF_FIELD(13, 8, hs_step_stop_cnt_dev);
  DEF_FIELD(5, 0, hs_step_start_cnt_dev);
  static auto Get() { return hwreg::RegisterAddr<HsStaStopAcTimingReg>(0x84); }
};

class SdaTimingReg : public hwreg::RegisterBase<SdaTimingReg, uint32_t> {
 public:
  DEF_BIT(12, sda_write_adj);
  DEF_FIELD(11, 6, hs_sda_write_time);
  DEF_FIELD(5, 0, sda_write_time);
  static auto Get() { return hwreg::RegisterAddr<SdaTimingReg>(0x88); }
};

class XoRegs : public ddk::MmioBuffer {
 public:
  explicit XoRegs(ddk::MmioBuffer mmio) : ddk::MmioBuffer(std::move(mmio)) {}

  // TODO(andresoportus): This should be part of a clock driver.
  void ClockEnable(uint32_t id, bool enable) {
    uint32_t bits[] = {3, 4, 16};
    static_assert(MT8167_I2C_CNT == std::size(bits));
    SetBit<uint32_t>(bits[id], enable ? 0x84 : 0x54);
  }
};

}  // namespace mt8167_i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_MT8167_I2C_MT8167_I2C_REGS_H_

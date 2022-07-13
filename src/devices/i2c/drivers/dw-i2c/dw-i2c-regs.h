// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_REGS_H_
#define SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_REGS_H_

#include <hwreg/bitfields.h>

namespace dw_i2c {

/* I2C Control */
class ControlReg : public hwreg::RegisterBase<ControlReg, uint32_t> {
 public:
  DEF_BIT(7, tx_empty_ctrl);
  DEF_BIT(6, slave_disable);
  DEF_BIT(5, restart_en);
  DEF_BIT(4, master_10bitaddr);
  DEF_BIT(3, slave_10bitaddr);
  DEF_FIELD(2, 1, max_speed_mode);
  DEF_BIT(0, master_mode);
  static auto Get() { return hwreg::RegisterAddr<ControlReg>(0x00); }
};

/* I2C Target Address */
class TargetAddressReg : public hwreg::RegisterBase<TargetAddressReg, uint32_t> {
 public:
  DEF_BIT(12, master_10bitaddr);
  DEF_BIT(11, special);
  DEF_BIT(10, gc_or_start);
  DEF_FIELD(9, 0, target_address);
  static auto Get() { return hwreg::RegisterAddr<TargetAddressReg>(0x04); }
};

/* I2C Slave Address */
class SlaveAddressReg : public hwreg::RegisterBase<SlaveAddressReg, uint32_t> {
 public:
  DEF_FIELD(9, 0, slave_address);
  static auto Get() { return hwreg::RegisterAddr<SlaveAddressReg>(0x08); }
};

/* I2C HS Master Mode Code Address */
class HSMasterAddrReg : public hwreg::RegisterBase<HSMasterAddrReg, uint32_t> {
 public:
  DEF_FIELD(2, 0, hs_master_code);
  static auto Get() { return hwreg::RegisterAddr<HSMasterAddrReg>(0x0c); }
};

/* I2C Rx/Tx Data Buffer and Command */
class DataCommandReg : public hwreg::RegisterBase<DataCommandReg, uint32_t> {
 public:
  DEF_BIT(10, start);
  DEF_BIT(9, stop);
  DEF_BIT(8, command);
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<DataCommandReg>(0x10); }
};

/* SS I2C Clock SCL High Count */
class StandardSpeedSclHighCountReg
    : public hwreg::RegisterBase<StandardSpeedSclHighCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, ss_scl_hcnt);
  static auto Get() { return hwreg::RegisterAddr<StandardSpeedSclHighCountReg>(0x14); }
};

/* SS I2C Clock SCL Low Count */
class StandardSpeedSclLowCountReg
    : public hwreg::RegisterBase<StandardSpeedSclLowCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, ss_scl_lcnt);
  static auto Get() { return hwreg::RegisterAddr<StandardSpeedSclLowCountReg>(0x18); }
};

/* Fast Mode I2C Clock SCL High Count */
class FastSpeedSclHighCountReg : public hwreg::RegisterBase<FastSpeedSclHighCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, fs_scl_hcnt);
  static auto Get() { return hwreg::RegisterAddr<FastSpeedSclHighCountReg>(0x1c); }
};

/* Fast Mode I2C Clock SCL Low Count */
class FastSpeedSclLowCountReg : public hwreg::RegisterBase<FastSpeedSclLowCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, fs_scl_lcnt);
  static auto Get() { return hwreg::RegisterAddr<FastSpeedSclLowCountReg>(0x20); }
};

/* High Speed I2C Clock SCL High Count */
class HighSpeedSclHighCountReg : public hwreg::RegisterBase<HighSpeedSclHighCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, hs_scl_hcnt);
  static auto Get() { return hwreg::RegisterAddr<HighSpeedSclHighCountReg>(0x24); }
};

/* High Speed I2C Clock SCL Low Count */
class HighSpeedSclLowCountReg : public hwreg::RegisterBase<HighSpeedSclLowCountReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, hs_scl_lcnt);
  static auto Get() { return hwreg::RegisterAddr<HighSpeedSclLowCountReg>(0x28); }
};

/* I2C Interrupt Status */
class InterruptStatusReg : public hwreg::RegisterBase<InterruptStatusReg, uint32_t> {
 public:
  DEF_BIT(14, scl_stuck_low);
  DEF_BIT(13, mstr_on_hold);
  DEF_BIT(12, restart_det);
  DEF_BIT(11, gen_call);
  DEF_BIT(10, start_det);
  DEF_BIT(9, stop_det);
  DEF_BIT(8, activity);
  DEF_BIT(7, rx_done);
  DEF_BIT(6, tx_abrt);
  DEF_BIT(5, rd_req);
  DEF_BIT(4, tx_empty);
  DEF_BIT(3, tx_over);
  DEF_BIT(2, rx_full);
  DEF_BIT(1, rx_over);
  DEF_BIT(0, rx_under);
  static auto Get() { return hwreg::RegisterAddr<InterruptStatusReg>(0x2c); }
};

/* I2C Interrupt Mask */
class InterruptMaskReg : public hwreg::RegisterBase<InterruptMaskReg, uint32_t> {
 public:
  DEF_BIT(14, scl_stuck_low);
  DEF_BIT(13, mstr_on_hold);
  DEF_BIT(12, restart_det);
  DEF_BIT(11, gen_call);
  DEF_BIT(10, start_det);
  DEF_BIT(9, stop_det);
  DEF_BIT(8, activity);
  DEF_BIT(7, rx_done);
  DEF_BIT(6, tx_abrt);
  DEF_BIT(5, rd_req);
  DEF_BIT(4, tx_empty);
  DEF_BIT(3, tx_over);
  DEF_BIT(2, rx_full);
  DEF_BIT(1, rx_over);
  DEF_BIT(0, rx_under);
  static auto Get() { return hwreg::RegisterAddr<InterruptMaskReg>(0x30); }
};

/* I2C Raw Interrupt Status */
class RawInterruptStatusReg : public hwreg::RegisterBase<RawInterruptStatusReg, uint32_t> {
 public:
  DEF_BIT(14, scl_stuck_low);
  DEF_BIT(13, mstr_on_hold);
  DEF_BIT(12, restart_det);
  DEF_BIT(11, gen_call);
  DEF_BIT(10, start_det);
  DEF_BIT(9, stop_det);
  DEF_BIT(8, activity);
  DEF_BIT(7, rx_done);
  DEF_BIT(6, tx_abrt);
  DEF_BIT(5, rd_req);
  DEF_BIT(4, tx_empty);
  DEF_BIT(3, tx_over);
  DEF_BIT(2, rx_full);
  DEF_BIT(1, rx_over);
  DEF_BIT(0, rx_under);
  static auto Get() { return hwreg::RegisterAddr<RawInterruptStatusReg>(0x34); }
};

/* I2C Receive FIFO Threshold */
class RxFifoThresholdReg : public hwreg::RegisterBase<RxFifoThresholdReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, rx_threshold_level);
  static auto Get() { return hwreg::RegisterAddr<RxFifoThresholdReg>(0x38); }
};

/* I2C Transmit FIFO Threshold */
class TxFifoThresholdReg : public hwreg::RegisterBase<TxFifoThresholdReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, tx_threshold_level);
  static auto Get() { return hwreg::RegisterAddr<TxFifoThresholdReg>(0x3c); }
};

/* Read this registers to clear the interrupt*/
class ClearInterruptReg : public hwreg::RegisterBase<ClearInterruptReg, uint32_t> {
 public:
  DEF_BIT(0, clr_intr);
  static auto Get() { return hwreg::RegisterAddr<ClearInterruptReg>(0x40); }
};

/* Read these registers to clear the corresponding bit in interrupt*/
class ClearRxUnderReg : public hwreg::RegisterBase<ClearRxUnderReg, uint32_t> {
 public:
  DEF_BIT(0, clr_rx_under);
  static auto Get() { return hwreg::RegisterAddr<ClearRxUnderReg>(0x44); }
};

class ClearRxOverReg : public hwreg::RegisterBase<ClearRxOverReg, uint32_t> {
 public:
  DEF_BIT(0, clr_rx_over);
  static auto Get() { return hwreg::RegisterAddr<ClearRxOverReg>(0x48); }
};

class ClearTxOverReg : public hwreg::RegisterBase<ClearTxOverReg, uint32_t> {
 public:
  DEF_BIT(0, clr_tx_over);
  static auto Get() { return hwreg::RegisterAddr<ClearTxOverReg>(0x4c); }
};

class ClearRdReqReg : public hwreg::RegisterBase<ClearRdReqReg, uint32_t> {
 public:
  DEF_BIT(0, clr_rd_req);
  static auto Get() { return hwreg::RegisterAddr<ClearRdReqReg>(0x50); }
};

class ClearTxAbrtReg : public hwreg::RegisterBase<ClearTxAbrtReg, uint32_t> {
 public:
  DEF_BIT(0, clr_tx_abrt);
  static auto Get() { return hwreg::RegisterAddr<ClearTxAbrtReg>(0x54); }
};

class ClearRxDoneReg : public hwreg::RegisterBase<ClearRxDoneReg, uint32_t> {
 public:
  DEF_BIT(0, clr_rx_done);
  static auto Get() { return hwreg::RegisterAddr<ClearRxDoneReg>(0x58); }
};

class ClearActivityReg : public hwreg::RegisterBase<ClearActivityReg, uint32_t> {
 public:
  DEF_BIT(0, clr_activity);
  static auto Get() { return hwreg::RegisterAddr<ClearActivityReg>(0x5c); }
};

class ClearStopDetReg : public hwreg::RegisterBase<ClearStopDetReg, uint32_t> {
 public:
  DEF_BIT(0, clr_stop_det);
  static auto Get() { return hwreg::RegisterAddr<ClearStopDetReg>(0x60); }
};

class ClearStartDetReg : public hwreg::RegisterBase<ClearStartDetReg, uint32_t> {
 public:
  DEF_BIT(0, clr_start_det);
  static auto Get() { return hwreg::RegisterAddr<ClearStartDetReg>(0x64); }
};

class ClearGenCallReg : public hwreg::RegisterBase<ClearGenCallReg, uint32_t> {
 public:
  DEF_BIT(0, clr_gen_call);
  static auto Get() { return hwreg::RegisterAddr<ClearGenCallReg>(0x68); }
};

/* I2C Enable */
class EnableReg : public hwreg::RegisterBase<EnableReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<EnableReg>(0x6c); }
};

/* I2C Status */
class StatusReg : public hwreg::RegisterBase<StatusReg, uint32_t> {
 public:
  DEF_BIT(6, slave_activity);
  DEF_BIT(5, master_activity);
  DEF_BIT(4, rx_fifo_full);
  DEF_BIT(3, rx_fifo_not_empty);
  DEF_BIT(2, tx_fifo_empty);
  DEF_BIT(1, tx_fifo_not_full);
  DEF_BIT(0, activity);
  static auto Get() { return hwreg::RegisterAddr<StatusReg>(0x70); }
};

/* I2C Transmit FIFO Level */
class TxFifoLevelReg : public hwreg::RegisterBase<TxFifoLevelReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, tx_fifo_level);
  static auto Get() { return hwreg::RegisterAddr<TxFifoLevelReg>(0x74); }
};

/* I2C Receive FIFO Level */
class RxFifoLevelReg : public hwreg::RegisterBase<RxFifoLevelReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, rx_fifo_level);
  static auto Get() { return hwreg::RegisterAddr<RxFifoLevelReg>(0x78); }
};

/* I2C SDA Hold Time Length */
class SdaHoldReg : public hwreg::RegisterBase<SdaHoldReg, uint32_t> {
 public:
  DEF_FIELD(15, 0, sda_hold_time_tx);
  DEF_FIELD(23, 16, sda_hold_time_rx);
  static auto Get() { return hwreg::RegisterAddr<SdaHoldReg>(0x7C); }
};

/* I2C Transmit Abort Source */
class TxAbrtSourceReg : public hwreg::RegisterBase<TxAbrtSourceReg, uint32_t> {
 public:
  DEF_BIT(15, abrt_slvrd_intx);
  DEF_BIT(14, abrt_slv_arblost);
  DEF_BIT(13, abrt_slvflush_txfifo);
  DEF_BIT(12, abrt_lost);
  DEF_BIT(11, abrt_master_dis);
  DEF_BIT(10, abrt_10b_rd_norstrt);
  DEF_BIT(9, abrt_sbyte_norstrt);
  DEF_BIT(8, abrt_hs_norstrt);
  DEF_BIT(7, abrt_sbyte_ackdet);
  DEF_BIT(6, abrt_hs_ackdet);
  DEF_BIT(5, abrt_gcall_read);
  DEF_BIT(4, abrt_gcall_noack);
  DEF_BIT(3, abrt_txdata_noack);
  DEF_BIT(2, abrt_10addr2_noack);
  DEF_BIT(1, abrt_10addr1_noack);
  DEF_BIT(0, abrt_7b_addr_noack);
  static auto Get() { return hwreg::RegisterAddr<TxAbrtSourceReg>(0x80); }
};

/* Generate Slave Data NACK */
class SlaveDataNackReg : public hwreg::RegisterBase<SlaveDataNackReg, uint32_t> {
 public:
  DEF_BIT(0, nack);
  static auto Get() { return hwreg::RegisterAddr<SlaveDataNackReg>(0x84); }
};

/* DMA COntrol */
class DMAControlReg : public hwreg::RegisterBase<DMAControlReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DMAControlReg>(0x88); }
};

/* DMA Transmit Data Level */
class DMATxDataLevelReg : public hwreg::RegisterBase<DMATxDataLevelReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DMATxDataLevelReg>(0x8c); }
};

/* DMA Receive Data Level */
class DMARxDataLevelReg : public hwreg::RegisterBase<DMARxDataLevelReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DMARxDataLevelReg>(0x90); }
};

/* I2C SDA Setup */
class SdaSetupReg : public hwreg::RegisterBase<SdaSetupReg, uint32_t> {
 public:
  DEF_BIT(0, sda_setup);
  static auto Get() { return hwreg::RegisterAddr<SdaSetupReg>(0x94); }
};

/* I2C ACK General Call */
class AckGeneralCallReg : public hwreg::RegisterBase<AckGeneralCallReg, uint32_t> {
 public:
  DEF_BIT(0, ack_gen_call);
  static auto Get() { return hwreg::RegisterAddr<AckGeneralCallReg>(0x98); }
};

/* I2C Enable Status */
class EnableStatusReg : public hwreg::RegisterBase<EnableStatusReg, uint32_t> {
 public:
  DEF_BIT(2, slv_fifo_filled_and_flushed);
  DEF_BIT(1, slv_rx_aborted);
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<EnableStatusReg>(0x9c); }
};

/* I2C FS Spike Suppression Limit Register*/
class FSSpikeLengthReg : public hwreg::RegisterBase<FSSpikeLengthReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, fs_spklen);
  static auto Get() { return hwreg::RegisterAddr<FSSpikeLengthReg>(0xA0); }
};

/* I2C HS Spike Suppression Limit Register*/
class HSSpikeLengthReg : public hwreg::RegisterBase<HSSpikeLengthReg, uint32_t> {
 public:
  DEF_FIELD(7, 0, hs_spklen);
  static auto Get() { return hwreg::RegisterAddr<HSSpikeLengthReg>(0xA4); }
};

/* Clear RESTART_DET Interrupt */
class ClearRestartDetReg : public hwreg::RegisterBase<ClearRestartDetReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ClearRestartDetReg>(0xa8); }
};

/* I2C SCL Stuck at Low Timeout */
class SclStuckAtLowTimeoutReg : public hwreg::RegisterBase<SclStuckAtLowTimeoutReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SclStuckAtLowTimeoutReg>(0xac); }
};

/* I2C SDA Stuck at Low Timeout */
class SdaStuckAtLowTimeoutReg : public hwreg::RegisterBase<SdaStuckAtLowTimeoutReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SdaStuckAtLowTimeoutReg>(0xb0); }
};

/* Clear SCL Stuck at Low Detect Intr */
class ClearSclStuckDetReg : public hwreg::RegisterBase<ClearSclStuckDetReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ClearSclStuckDetReg>(0xb4); }
};

/* I2C Device-ID */
class DeviceIDReg : public hwreg::RegisterBase<DeviceIDReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<DeviceIDReg>(0xb8); }
};

/* SMBus Slave Clock Extend Timeout */
class SMBusClkLowSextReg : public hwreg::RegisterBase<SMBusClkLowSextReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusClkLowSextReg>(0xbc); }
};

/* SMBus Master Clock Extend Timeout */
class SMBusClkLowMextReg : public hwreg::RegisterBase<SMBusClkLowMextReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusClkLowMextReg>(0xc0); }
};

/* SMBus Master High MAX Bus-idle cnt */
class SMBusTHighMaxIdleCountReg : public hwreg::RegisterBase<SMBusTHighMaxIdleCountReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusTHighMaxIdleCountReg>(0xc4); }
};

/* SMBUS Interrupt Status */
class SMBusIntrStatReg : public hwreg::RegisterBase<SMBusIntrStatReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusIntrStatReg>(0xc8); }
};

/* SMBus Interrupt Mask */
class SMBusIntrMaskReg : public hwreg::RegisterBase<SMBusIntrMaskReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusIntrMaskReg>(0xcc); }
};

/* SMBus Raw Interrupt Status */
class SMBusRawIntrStatReg : public hwreg::RegisterBase<SMBusRawIntrStatReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusRawIntrStatReg>(0xd0); }
};

/* SMBus Clear Interrupt */
class ClearSMBusIntrReg : public hwreg::RegisterBase<ClearSMBusIntrReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ClearSMBusIntrReg>(0xd4); }
};

/* I2C Optional Slave Address */
class OptionalSARReg : public hwreg::RegisterBase<OptionalSARReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<OptionalSARReg>(0xd8); }
};

/* SMBUS ARP UDID LSB */
class SMBusUdidLsbReg : public hwreg::RegisterBase<SMBusUdidLsbReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<SMBusUdidLsbReg>(0xdc); }
};

/* Fragment Parameter */
class CompParam1Reg : public hwreg::RegisterBase<CompParam1Reg, uint32_t> {
 public:
  DEF_FIELD(23, 16, tx_buffer_depth);
  DEF_FIELD(15, 8, rx_buffer_depth);
  DEF_BIT(7, add_encoded_params);
  DEF_BIT(6, has_dma);
  DEF_BIT(5, intr_io);
  DEF_BIT(4, hc_count_values);
  DEF_FIELD(3, 2, max_speed_mode);
  DEF_FIELD(1, 0, apb_data_width);
  static auto Get() { return hwreg::RegisterAddr<CompParam1Reg>(0xf4); }
};

/* I2C Fragment Version */
class CompVersionReg : public hwreg::RegisterBase<CompVersionReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CompVersionReg>(0xf8); }
};

/* I2C Fragment Type */
class CompTypeReg : public hwreg::RegisterBase<CompTypeReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CompTypeReg>(0xfc); }
};

}  // namespace dw_i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_REGS_H_

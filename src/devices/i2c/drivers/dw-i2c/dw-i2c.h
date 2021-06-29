// Copyright 2019 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_H_
#define SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_H_

#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/event.h>
#include <lib/zx/interrupt.h>

#include <memory>
#include <thread>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "dw-i2c-regs.h"

namespace dw_i2c {

class DwI2c;
class DwI2cBus;

using DeviceType = ddk::Device<DwI2c, ddk::Unbindable>;

class DwI2c : public DeviceType, public ddk::I2cImplProtocol<DwI2c, ddk::base_protocol> {
 public:
  explicit DwI2c(zx_device_t* parent, fbl::Vector<std::unique_ptr<DwI2cBus>>&& bus_list)
      : DeviceType(parent), buses_(std::move(bus_list)), bus_count_(buses_.size()) {}
  ~DwI2c() = default;

  zx_status_t Bind();
  zx_status_t Init();
  static zx_status_t Create(void* ctx, zx_device_t* parent);
  void ShutDown();

  // Methods required by the ddk mixins.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  uint32_t I2cImplGetBusBase();
  uint32_t I2cImplGetBusCount();
  zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size);
  zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate);
  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count);

 private:
  int TestThread();

  fbl::Vector<std::unique_ptr<DwI2cBus>> buses_;
  size_t bus_count_ = 0;
};

class DwI2cBus {
 public:
  static constexpr uint32_t kDwCompTypeNum = 0x44570140;
  // Local buffer for transfer and receive. Matches FIFO size.
  uint32_t kMaxTransfer = 0;

  explicit DwI2cBus(ddk::MmioBuffer mmio, zx::interrupt& irq)
      : mmio_(std::move(mmio)), irq_(std::move(irq)) {}

  DwI2cBus() = delete;
  ~DwI2cBus() { ShutDown(); }

  zx_status_t Init();
  void ShutDown();
  zx_status_t Transact(const i2c_impl_op_t* rws, size_t count);

 private:
  static constexpr uint32_t kErrorSignal = ZX_USER_SIGNAL_0;
  static constexpr uint32_t kTransactionCompleteSignal = ZX_USER_SIGNAL_1;
  static constexpr uint32_t kI2cDisable = 0;
  static constexpr uint32_t kI2cEnable = 1;
  static constexpr uint32_t kStandardMode = 1;
  static constexpr uint32_t kFastMode = 2;
  static constexpr uint32_t kHighSpeedMode = 3;
  static constexpr uint32_t k7BitAddr = 0;
  static constexpr uint16_t k7BitAddrMask = 0x7f;
  static constexpr uint32_t k10BitAddr = 0;
  static constexpr uint32_t kActive = 1;
  static constexpr uint32_t kMaxPoll = 100;
  static constexpr uint32_t kPollSleep = ZX_USEC(25);
  static constexpr uint32_t kDefaultTimeout = ZX_MSEC(100);
  uint32_t kI2cInterruptReadMask = InterruptMaskReg::Get()
                                       .FromValue(0)
                                       .set_rx_full(1)
                                       .set_tx_abrt(1)
                                       .set_stop_det(1)
                                       .reg_value();
  uint32_t kI2cInterruptDefaultMask = InterruptMaskReg::Get()
                                          .FromValue(0)
                                          .set_rx_full(1)
                                          .set_tx_abrt(1)
                                          .set_stop_det(1)
                                          .set_tx_empty(1)
                                          .reg_value();

  /* I2C timing parameters */
  static constexpr uint32_t kClkRateKHz = 100000;
  static constexpr uint32_t kSclTFalling = 205;
  static constexpr uint32_t kSdaTFalling = 425;
  static constexpr uint32_t kSdaTHold = 449;
  /* Standard speed parameters */
  static constexpr uint32_t kSclStandardSpeedTHold = 4000;  // SCL hold time for start signal in ns
  static constexpr uint32_t kSclStandardSpeedTLow = 4700;   // SCL low time in ns
  /* Fast speed parameters */
  static constexpr uint32_t kSclFastSpeedTHold = 600;  // SCL hold time for start signal in ns
  static constexpr uint32_t kSclFastSpeedTLow = 1300;  // SCL low time in ns

  // IC_[FS]S_SCL_HCNT + 3 >= IC_CLK * (tHD;STA + tf)
  static constexpr uint32_t kSclStandardSpeedHcnt =
      ((kClkRateKHz * (kSclStandardSpeedTHold + kSdaTFalling)) + 500000) / 1000000 - 3;
  static constexpr uint32_t kSclFastSpeedHcnt =
      ((kClkRateKHz * (kSclFastSpeedTHold + kSdaTFalling)) + 500000) / 1000000 - 3;

  // IC_[FS]S_SCL_LCNT + 1 >= IC_CLK * (tLOW + tf)
  static constexpr uint32_t kSclStandardSpeedLcnt =
      ((kClkRateKHz * (kSclStandardSpeedTLow + kSclTFalling)) + 500000) / 1000000 - 1;
  static constexpr uint32_t kSclFastSpeedLcnt =
      ((kClkRateKHz * (kSclFastSpeedTLow + kSclTFalling)) + 500000) / 1000000 - 1;

  // IC_SDA_HOLD = (IC_CLK * tSDA;Hold + 500000 / 1000000)
  static constexpr uint32_t kSdaHoldValue = ((kClkRateKHz * kSdaTHold) + 500000) / 1000000;

  zx_status_t HostInit();
  zx_status_t Receive() __TA_REQUIRES(ops_lock_);
  zx_status_t Transmit() __TA_REQUIRES(ops_lock_);
  zx_status_t SetSlaveAddress(uint16_t addr);
  zx_status_t Dumpstate();
  zx_status_t EnableAndWait(bool enable);
  zx_status_t Enable();
  void ClearInterrupts();
  void DisableInterrupts();
  void EnableInterrupts(uint32_t flag);
  zx_status_t Disable();
  zx_status_t WaitEvent(uint32_t sig_mask);
  InterruptStatusReg ReadAndClearIrq();
  int IrqThread();
  zx_status_t WaitBusBusy();
  void SetOpsHelper(const i2c_impl_op_t* ops, size_t count);

  ddk::MmioBuffer mmio_;
  zx::interrupt irq_;
  zx::event event_;
  zx::duration timeout_;
  std::thread irq_thread_;
  uint32_t tx_fifo_depth_ = 0;
  uint32_t rx_fifo_depth_ = 0;

  fbl::Mutex transact_lock_; /* used to serialize transactions */
  fbl::Mutex ops_lock_;      /* used to set ops for irq thread */

  const i2c_impl_op_t* ops_ __TA_GUARDED(ops_lock_) = nullptr;
  size_t ops_count_ __TA_GUARDED(ops_lock_) = 0;
  uint32_t rx_op_idx_ __TA_GUARDED(ops_lock_) = 0;
  uint32_t tx_op_idx_ __TA_GUARDED(ops_lock_) = 0;
  uint32_t rx_done_len_ __TA_GUARDED(ops_lock_) = 0;
  uint32_t tx_done_len_ __TA_GUARDED(ops_lock_) = 0;
  uint32_t rx_pending_ __TA_GUARDED(ops_lock_) = 0;
  bool send_restart_ __TA_GUARDED(ops_lock_) = false;
};

}  // namespace dw_i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_DW_I2C_DW_I2C_H_

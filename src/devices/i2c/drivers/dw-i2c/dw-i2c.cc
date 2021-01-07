// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dw-i2c.h"

#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "src/devices/i2c/drivers/dw-i2c/dw_i2c-bind.h"

namespace dw_i2c {

zx_status_t DwI2cBus::Dumpstate() {
  zxlogf(INFO, "DW_kI2cEnable_STATUS = \t0x%x",
         EnableStatusReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_kI2cEnable = \t0x%x", EnableReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_CON = \t0x%x", ControlReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_TAR = \t0x%x", TargetAddressReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_HS_MADDR = \t0x%x", HSMasterAddrReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_SS_SCL_HCNT = \t0x%x",
         StandardSpeedSclHighCountReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_SS_SCL_LCNT = \t0x%x",
         StandardSpeedSclLowCountReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_FS_SCL_HCNT = \t0x%x",
         FastSpeedSclHighCountReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_FS_SCL_LCNT = \t0x%x",
         FastSpeedSclLowCountReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_INTR_MASK = \t0x%x", InterruptMaskReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_RAW_INTR_STAT = \t0x%x",
         RawInterruptStatusReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_RX_TL = \t0x%x", RxFifoThresholdReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_TX_TL = \t0x%x", TxFifoThresholdReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_STATUS = \t0x%x", StatusReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_TXFLR = \t0x%x", TxFifoLevelReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_RXFLR = \t0x%x", RxFifoLevelReg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_COMP_PARAM_1 = \t0x%x", CompParam1Reg::Get().ReadFrom(&mmio_).reg_value());
  zxlogf(INFO, "DW_I2C_TX_ABRT_SOURCE = \t0x%x",
         TxAbrtSourceReg::Get().ReadFrom(&mmio_).reg_value());
  return ZX_OK;
}

zx_status_t DwI2cBus::EnableAndWait(bool enable) {
  uint32_t poll = 0;

  // Set enable bit.
  auto enable_reg = EnableReg::Get().ReadFrom(&mmio_);
  enable_reg.set_enable(enable).WriteTo(&mmio_);

  do {
    if (EnableStatusReg::Get().ReadFrom(&mmio_).enable() == enable) {
      // We are done. Exit.
      return ZX_OK;
    }
    // Sleep 10 times the signaling period for the highest i2c transfer speed (400K) ~25uS.
    zx_nanosleep(zx_deadline_after(kPollSleep));
  } while (poll++ < kMaxPoll);

  zxlogf(ERROR, "%s: Could not %s I2C contoller! DW_kI2cEnable_STATUS = 0x%x", __FUNCTION__,
         enable ? "enable" : "disable", EnableStatusReg::Get().ReadFrom(&mmio_).enable());
  Dumpstate();

  return ZX_ERR_TIMED_OUT;
}

zx_status_t DwI2cBus::Enable() { return EnableAndWait(kI2cEnable); }

void DwI2cBus::ClearInterrupts() {
  // Reading this register will clear all the interrupts.
  ClearInterruptReg::Get().ReadFrom(&mmio_);
}

void DwI2cBus::DisableInterrupts() { InterruptMaskReg::Get().FromValue(0).WriteTo(&mmio_); }

void DwI2cBus::EnableInterrupts(uint32_t flag) {
  InterruptMaskReg::Get().FromValue(flag).WriteTo(&mmio_);
}

zx_status_t DwI2cBus::Disable() { return EnableAndWait(kI2cDisable); }

zx_status_t DwI2cBus::WaitEvent(uint32_t sig_mask) {
  uint32_t observed = 0;
  auto deadline = zx::deadline_after(timeout_);
  sig_mask |= kErrorSignal;

  zx_status_t status = event_.wait_one(sig_mask, deadline, &observed);
  if (status != ZX_OK) {
    return status;
  }

  event_.signal(observed, 0);
  if (observed & kErrorSignal) {
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

InterruptStatusReg DwI2cBus::ReadAndClearIrq() {
  auto irq = InterruptStatusReg::Get().ReadFrom(&mmio_);

  if (irq.tx_abrt()) {
    // ABRT_SOURCE should be read before clearing TX_ABRT.
    zxlogf(ERROR, "dw-i2c: error on bus - Abort source 0x%x",
           TxAbrtSourceReg::Get().ReadFrom(&mmio_).reg_value());
    ClearTxAbrtReg::Get().ReadFrom(&mmio_);
  }
  if (irq.start_det()) {
    ClearStartDetReg::Get().ReadFrom(&mmio_);
  }
  if (irq.activity()) {
    ClearActivityReg::Get().ReadFrom(&mmio_);
  }
  if (irq.stop_det()) {
    ClearStopDetReg::Get().ReadFrom(&mmio_);
  }
  return irq;
}

// Thread to handle interrupts.
int DwI2cBus::IrqThread() {
  zx_status_t status;

  while (1) {
    status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq wait failed, retcode = %d", __FUNCTION__, status);
      return status;
    }

    fbl::AutoLock lock(&ops_lock_);
    if (ops_ == nullptr) {
      continue;
    }

    auto reg = ReadAndClearIrq();

    if (reg.tx_abrt()) {
      if (event_.signal(0, kErrorSignal) != ZX_OK) {
        zxlogf(ERROR, "Failure signaling I2C error - %d", status);
      }
      ops_ = nullptr;
    }

    if (reg.rx_full()) {
      if (Receive() != ZX_OK) {
        if (event_.signal(0, kErrorSignal) != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C error - %d", status);
        }
        ops_ = nullptr;
      }
    }

    if (reg.tx_empty()) {
      if (Transmit() != ZX_OK) {
        if (event_.signal(0, kErrorSignal) != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C error - %d", status);
        }
        ops_ = nullptr;
      }
    }

    if (reg.stop_det()) {
      // Signal complete when all tx/rx are complete.
      if (tx_op_idx_ == ops_count_ && rx_pending_ == 0) {
        if (event_.signal(0, kTransactionCompleteSignal) != ZX_OK) {
          zxlogf(ERROR, "Failure signaling I2C error - %d", status);
        }
        ops_ = nullptr;
      }
    }

  }  // while (1)

  return ZX_OK;
}

zx_status_t DwI2cBus::WaitBusBusy() {
  uint32_t timeout = 0;
  auto status = StatusReg::Get();
  while (status.ReadFrom(&mmio_).activity()) {
    if (timeout > 100) {
      return ZX_ERR_TIMED_OUT;
    }
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    timeout++;
  }
  return ZX_OK;
}

void DwI2cBus::SetOpsHelper(const i2c_impl_op_t* ops, size_t count) {
  fbl::AutoLock lock(&ops_lock_);
  ops_ = ops;
  ops_count_ = count;
  rx_op_idx_ = 0;
  tx_op_idx_ = 0;
  rx_done_len_ = 0;
  tx_done_len_ = 0;
  send_restart_ = false;
  rx_pending_ = 0;
}

zx_status_t DwI2cBus::Transact(const i2c_impl_op_t* rws, size_t count) {
  fbl::AutoLock lock(&transact_lock_);

  auto status = WaitBusBusy();
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C bus wait failed %d", status);
    return status;
  }
  status = SetSlaveAddress(rws[0].address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C set address failed %d", status);
    return status;
  }

  DisableInterrupts();
  SetOpsHelper(rws, count);
  status = Enable();
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2C device enable failed %d", status);
    return status;
  }
  event_.signal(kTransactionCompleteSignal | kErrorSignal, 0);
  ClearInterrupts();
  EnableInterrupts(kI2cInterruptDefaultMask);

  status = WaitEvent(kTransactionCompleteSignal);

  auto disable_ret = Disable();
  if (disable_ret != ZX_OK) {
    zxlogf(ERROR, "I2C device disable failed %d", disable_ret);
  }
  return status;
}

zx_status_t DwI2cBus::SetSlaveAddress(uint16_t addr) {
  if (addr & (~k7BitAddrMask)) {
    // support 7bit for now
    return ZX_ERR_NOT_SUPPORTED;
  }
  addr &= k7BitAddrMask;
  auto reg = TargetAddressReg::Get().ReadFrom(&mmio_);
  reg.set_target_address(addr).set_master_10bitaddr(0);
  reg.WriteTo(&mmio_);
  return ZX_OK;
}

zx_status_t DwI2cBus::Receive() {
  if (rx_pending_ == 0) {
    zxlogf(ERROR, "dw-i2c: Bytes received without being requested");
    return ZX_ERR_IO_OVERRUN;
  }

  uint32_t avail_read = RxFifoLevelReg::Get().ReadFrom(&mmio_).rx_fifo_level();

  while ((avail_read != 0) && (rx_op_idx_ < ops_count_)) {
    const i2c_impl_op_t* op = &ops_[rx_op_idx_];
    if (!op->is_read) {
      rx_op_idx_++;
      continue;
    }
    uint8_t* buff = static_cast<uint8_t*>(op->data_buffer) + rx_done_len_;
    *buff = static_cast<uint8_t>(DataCommandReg::Get().ReadFrom(&mmio_).data());
    rx_done_len_++;
    rx_pending_--;
    if (rx_done_len_ == op->data_size) {
      rx_op_idx_++;
      rx_done_len_ = 0;
    }
    avail_read--;
  }

  if (avail_read != 0) {
    zxlogf(ERROR, "dw-i2c: %d more bytes received than requested", avail_read);
    return ZX_ERR_IO_OVERRUN;
  }

  return ZX_OK;
}

zx_status_t DwI2cBus::Transmit() {
  uint32_t tx_limit;

  tx_limit = tx_fifo_depth_ - TxFifoLevelReg::Get().ReadFrom(&mmio_).tx_fifo_level();

  // TODO(fxbug.dev/34403)
  // If IC_EMPTYFIFO_HOLD_MASTER_EN = 0, then STOP is sent on TX_EMPTY. All commands should be
  // queued up as soon as possible to avoid this. Possible race leading to failed
  // transaction, if the irq thread is deschedule in the midst for tx command queuing.
  // This is the mode used in as370 and currently this issue is not addressed.
  // See bug fxbug.dev/34403 for details.
  // If IC_EMPTYFIFO_HOLD_MASTER_EN = 1, then STOP and RESTART must be sent explicitly, which is
  // handled by this code.
  while ((tx_limit != 0) && (tx_op_idx_ < ops_count_)) {
    const i2c_impl_op_t* op = &ops_[tx_op_idx_];
    uint8_t* buff = static_cast<uint8_t*>(op->data_buffer) + tx_done_len_;
    size_t len = op->data_size - tx_done_len_;
    ZX_DEBUG_ASSERT(len <= kMaxTransfer);

    auto cmd = DataCommandReg::Get().FromValue(0);
    // Send STOP cmd if last byte and stop set.
    if (len == 1 && op->stop) {
      cmd.set_stop(1);
    }
    // Send restart.
    if (send_restart_) {
      cmd.set_start(1);
      send_restart_ = false;
    }

    if (op->is_read) {
      // Read command should be queued for each byte.
      cmd.set_command(1);
      rx_pending_++;
      // Set receive threshold to 1 less than the expected size.
      // Do this only once.
      if (tx_done_len_ == 0) {
        RxFifoThresholdReg::Get()
            .FromValue(0)
            .set_rx_threshold_level(static_cast<uint32_t>(op->data_size - 1))
            .WriteTo(&mmio_);
      }
    } else {
      cmd.set_data(*buff++);
    }
    cmd.WriteTo(&mmio_);
    tx_done_len_++;

    if (tx_done_len_ == op->data_size) {
      tx_op_idx_++;
      tx_done_len_ = 0;
      send_restart_ = true;
    }
    tx_limit--;
  }

  if (tx_op_idx_ == ops_count_) {
    // All tx are complete. Remove TX_EMPTY from interrupt mask.
    EnableInterrupts(kI2cInterruptReadMask);
  }

  return ZX_OK;
}

void DwI2cBus::ShutDown() {
  irq_.destroy();
  if (irq_thread_.joinable()) {
    irq_thread_.join();
  }
}

zx_status_t DwI2cBus::HostInit() {
  // Make sure we are truly running on a DesignWire IP.
  auto dw_comp_type = CompTypeReg::Get().ReadFrom(&mmio_).reg_value();

  if (dw_comp_type != kDwCompTypeNum) {
    zxlogf(ERROR, "%s: Incompatible IP Block detected. Expected = 0x%x, Actual = 0x%x",
           __FUNCTION__, kDwCompTypeNum, dw_comp_type);

    return ZX_ERR_NOT_SUPPORTED;
  }

  // Read the various capabilities of the fragment.
  auto comp_reg = CompParam1Reg::Get().ReadFrom(&mmio_);
  tx_fifo_depth_ = comp_reg.tx_buffer_depth();
  rx_fifo_depth_ = comp_reg.rx_buffer_depth();

  // Minimum fifo depth would be max transfer limit.
  kMaxTransfer = tx_fifo_depth_ > rx_fifo_depth_ ? rx_fifo_depth_ : tx_fifo_depth_;

  /* I2C Block Initialization based on DW_apb_i2c_databook Section 7.3 */

  // Disable I2C Block.
  Disable();

  // Configure the controller:
  auto ctrl_reg = ControlReg::Get().FromValue(0);

  // - slave disable
  ctrl_reg.set_slave_disable(1);

  // - enable restart mode
  ctrl_reg.set_restart_en(1);

  // - set 7-bit address modeset
  ctrl_reg.set_master_10bitaddr(k7BitAddr);
  ctrl_reg.set_slave_10bitaddr(k7BitAddr);

  // - set speed to fast, master enable
  ctrl_reg.set_max_speed_mode(kFastMode);

  // - set master enable
  ctrl_reg.set_master_mode(1);

  // Write final mask.
  ctrl_reg.WriteTo(&mmio_);

  // Write SS/FS LCNT and HCNT.
  StandardSpeedSclHighCountReg::Get()
      .ReadFrom(&mmio_)
      .set_ss_scl_hcnt(kSclStandardSpeedHcnt)
      .WriteTo(&mmio_);
  StandardSpeedSclLowCountReg::Get()
      .ReadFrom(&mmio_)
      .set_ss_scl_lcnt(kSclStandardSpeedLcnt)
      .WriteTo(&mmio_);
  FastSpeedSclHighCountReg::Get()
      .ReadFrom(&mmio_)
      .set_fs_scl_hcnt(kSclFastSpeedHcnt)
      .WriteTo(&mmio_);
  FastSpeedSclLowCountReg::Get()
      .ReadFrom(&mmio_)
      .set_fs_scl_lcnt(kSclFastSpeedLcnt)
      .WriteTo(&mmio_);

  // Set SDA Hold time.
  // Enable SDA hold for RX as well.
  SdaHoldReg::Get()
      .FromValue(0)
      .set_sda_hold_time_tx(kSdaHoldValue)
      .set_sda_hold_time_rx(kSdaHoldValue)
      .WriteTo(&mmio_);

  // Setup TX and RX FIFO Thresholds.
  TxFifoThresholdReg::Get()
      .ReadFrom(&mmio_)
      .set_tx_threshold_level(tx_fifo_depth_ / 2)
      .WriteTo(&mmio_);
  RxFifoThresholdReg::Get().ReadFrom(&mmio_).set_rx_threshold_level(0).WriteTo(&mmio_);

  // Disable interrupts.
  DisableInterrupts();

  return ZX_OK;
}

zx_status_t DwI2cBus::Init() {
  zx_status_t status;

  timeout_ = zx::duration(kDefaultTimeout);

  status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    return status;
  }

  // Initialize i2c host controller.
  status = HostInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to initialize i2c host controller %d", __FUNCTION__, status);
    return status;
  }

  irq_thread_ = std::thread(&DwI2cBus::IrqThread, this);

  return ZX_OK;
}

#define I2C_AS370_DW_TEST 0

#if I2C_AS370_DW_TEST
int DwI2c::TestThread() {
  zx_status_t status;
  bool pass = true;
  uint8_t addr = 0;            // PMIC device
  uint8_t valid_addr = 0x66;   // SY20212DAIC PMIC device
  uint8_t valid_value = 0x8B;  // Register 0x0 default value for PMIC
  uint8_t data_write = 0;
  uint8_t data_read;
#if 0
  zxlogf(INFO, "Finding I2c devices");
  // Find all available devices.
  for (uint32_t i = 0x0; i <= 0x7f; i++) {
    addr = static_cast<uint8_t>(i);
    i2c_impl_op_t ops[] = {
        {.address = addr,
         .data_buffer = &data_write,
         .data_size = 1,
         .is_read = false,
         .stop = false},
        {.address = addr, .data_buffer = &data_read, .data_size = 1, .is_read = true, .stop = true},
    };

    status = I2cImplTransact(0, ops, countof(ops));
    if (status == ZX_OK) {
      zxlogf(INFO, "I2C device found at address: 0x%02X", addr);
    }
  }
#endif
  zxlogf(INFO, "I2C: Testing PMIC ping");

  // Test multiple reads from a known device.
  for (uint32_t i = 0; i < 10; i++) {
    addr = valid_addr;
    data_read = 0;
    i2c_impl_op_t ops[] = {
        {.address = addr,
         .data_buffer = &data_write,
         .data_size = 1,
         .is_read = false,
         .stop = false},
        {.address = addr, .data_buffer = &data_read, .data_size = 1, .is_read = true, .stop = true},
    };

    status = I2cImplTransact(0, ops, countof(ops));
    if (status == ZX_OK) {
      // Check with reset value of PMIC registers.
      if (data_read != valid_value) {
        zxlogf(INFO, "I2C test: PMIC register value does not matched - %x", data_read);
        pass = false;
      }
    } else {
      zxlogf(INFO, "I2C test: PMIC ping failed : %d", status);
      pass = false;
    }
  }

  if (pass) {
    zxlogf(INFO, "DW I2C test for AS370 passed");
  } else {
    zxlogf(ERROR, "DW I2C test for AS370 failed");
  }
  return 0;
}
#endif

zx_status_t DwI2c::I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* rws, size_t count) {
  if (bus_id >= bus_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t i = 0; i < count; ++i) {
    if (rws[i].data_size > buses_[bus_id]->kMaxTransfer) {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  if (count == 0) {
    return ZX_OK;
  }

  for (uint32_t i = 1; i < count; ++i) {
    if (rws[i].address != rws[0].address) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  return buses_[bus_id]->Transact(rws, count);
}

zx_status_t DwI2c::I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) {
  // TODO: currently supports FAST_MODE - 400kHz
  return ZX_ERR_NOT_SUPPORTED;
}

uint32_t DwI2c::I2cImplGetBusCount() { return static_cast<uint32_t>(bus_count_); }

zx_status_t DwI2c::I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
  if (bus_id >= bus_count_) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_size = buses_[bus_id]->kMaxTransfer;
  return ZX_OK;
}

void DwI2c::ShutDown() {
  for (uint32_t id = 0; id < bus_count_; id++) {
    buses_[id]->ShutDown();
  }
}

void DwI2c::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void DwI2c::DdkRelease() { delete this; }

zx_status_t DwI2c::Init() {
  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

#if I2C_AS370_DW_TEST
  thrd_t test_thread;
  auto thunk = [](void* arg) -> int { return reinterpret_cast<DwI2c*>(arg)->TestThread(); };
  int rc = thrd_create_with_name(&test_thread, thunk, this, "dw-i2c-test");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
#endif

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t DwI2c::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  fbl::AllocChecker ac;

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PDEV", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t info;
  if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
    zxlogf(ERROR, "dw_i2c: pdev_get_device_info failed");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (info.mmio_count != info.irq_count) {
    zxlogf(ERROR, "dw_i2c: mmio_count %u does not matchirq_count %u", info.mmio_count,
           info.irq_count);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::Vector<std::unique_ptr<DwI2cBus>> bus_list;

  for (uint32_t i = 0; i < info.mmio_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed %d", __FUNCTION__, status);
      return status;
    }

    zx::interrupt irq;
    if ((status = pdev.GetInterrupt(i, &irq)) != ZX_OK) {
      return status;
    }

    auto i2c_bus = fbl::make_unique_checked<DwI2cBus>(&ac, *std::move(mmio), irq);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    if ((status = i2c_bus->Init()) != ZX_OK) {
      zxlogf(ERROR, "dw_i2c: dw_i2c bus init failed: %d", status);
      return ZX_ERR_INTERNAL;
    }

    bus_list.push_back(std::move(i2c_bus), &ac);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  auto dev = fbl::make_unique_checked<DwI2c>(&ac, parent, std::move(bus_list));
  if (!ac.check()) {
    zxlogf(ERROR, "%s ZX_ERR_NO_MEMORY", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->DdkAdd("dw-i2c")) != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FUNCTION__, status);
    dev->ShutDown();
    return status;
  }

  // Devmgr is now in charge of the memory for dev.
  auto ptr = dev.release();
  return ptr->Init();
}

static zx_driver_ops_t dw_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = DwI2c::Create,
    .create = nullptr,
    .release = nullptr,
    .run_unit_tests = nullptr,
};

}  // namespace dw_i2c

// clang-format off
ZIRCON_DRIVER(dw_i2c, dw_i2c::dw_i2c_driver_ops, "zircon", "0.1");

//clang-format on

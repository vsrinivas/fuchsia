// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_CONTROLLER_H_
#define SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_CONTROLLER_H_

#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/interrupt.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <map>
#include <optional>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>

#include "intel-i2c-subordinate.h"

namespace intel_i2c {
struct __attribute__((packed)) I2cRegs {
  uint32_t ctl;
  uint32_t tar_add;
  uint32_t _reserved0[2];
  uint32_t data_cmd;
  uint32_t ss_scl_hcnt;
  uint32_t ss_scl_lcnt;
  uint32_t fs_scl_hcnt;
  uint32_t fs_scl_lcnt;
  uint32_t _reserved1[2];
  uint32_t intr_stat;
  uint32_t intr_mask;
  uint32_t raw_intr_stat;
  uint32_t rx_tl;
  uint32_t tx_tl;
  uint32_t clr_intr;
  uint32_t clr_rx_under;
  uint32_t clr_rx_over;
  uint32_t clr_tx_over;
  uint32_t _reserved2[1];
  uint32_t clr_tx_abort;
  uint32_t _reserved3[1];
  uint32_t clr_activity;
  uint32_t clr_stop_det;
  uint32_t clr_start_det;
  uint32_t clr_gen_call;
  uint32_t i2c_en;
  uint32_t i2c_sta;
  uint32_t txflr;
  uint32_t rxflr;
  uint32_t sda_hold;
  uint32_t tx_abrt_source;
  uint32_t slv_data_nack;
  uint32_t dma_ctrl;
  uint32_t dma_tdlr;
  uint32_t dma_rdlr;
  uint32_t sda_setup;
  uint32_t ack_gen_call;
  uint32_t enable_status;
  uint32_t _reserved4[21];
  uint32_t comp_param1;
  uint32_t comp_ver;
  uint32_t comp_type;
};
_Static_assert(sizeof(I2cRegs) <= 0x200, "bad struct");

inline constexpr uint32_t kI2cMaxFastPlusSpeedHz = 1000000;
inline constexpr uint32_t kI2cMaxFastSpeedHz = 400000;
inline constexpr uint32_t kI2cMaxStandardSpeedHz = 100000;

inline constexpr uint32_t kI2cEnAbort = 1;
inline constexpr uint32_t kI2cEnEnable = 0;

inline constexpr uint32_t kCtlSlaveDisable = 6;
inline constexpr uint32_t kCtlRestartEnable = 5;
inline constexpr uint32_t kCtlAddressingMode = 4;

inline constexpr uint32_t kCtlAddressingMode7Bit = 0x0;
inline constexpr uint32_t kCtlAddressingMode10Bit = 0x1;

inline constexpr uint32_t kCtlSpeed = 1;
inline constexpr uint32_t kCtlSpeedStandard = 0x1;
inline constexpr uint32_t kCtlSpeedFast = 0x2;

inline constexpr uint32_t kCtlMasterMode = 0;
inline constexpr uint32_t kCtlMasterModeEnabled = 0x1;

inline constexpr uint32_t kIntrGeneralCall = 11;
inline constexpr uint32_t kIntrStartDetection = 10;
inline constexpr uint32_t kIntrStopDetection = 9;
inline constexpr uint32_t kIntrActivity = 8;
inline constexpr uint32_t kIntrTxAbort = 6;
inline constexpr uint32_t kIntrTxEmpty = 4;
inline constexpr uint32_t kIntrTxOver = 3;
inline constexpr uint32_t kIntrRxFull = 2;
inline constexpr uint32_t kIntrRxOver = 1;
inline constexpr uint32_t kIntrRxUnder = 0;

inline constexpr uint32_t kTarAddWidth = 12;
inline constexpr uint32_t kTarAddWidth7Bit = 0x0;
inline constexpr uint32_t kTarAddWidth10Bit = 0x1;

inline constexpr uint32_t kTarAddSpecial = 11;
inline constexpr uint32_t kTarAddGcOrStart = 10;
inline constexpr uint32_t kTarAddIcTar = 0;

inline constexpr uint32_t kI2cStaCa = 5;
inline constexpr uint32_t kI2cStaRfcf = 4;
inline constexpr uint32_t kI2cStaRfne = 3;
inline constexpr uint32_t kI2cStaTfce = 2;
inline constexpr uint32_t kI2cStaTfnf = 1;
inline constexpr uint32_t kI2cStaActivity = 0;

inline constexpr uint32_t kDataCmdRestart = 10;
inline constexpr uint32_t kDataCmdStop = 9;

inline constexpr uint32_t kDataCmdCmd = 8;
inline constexpr uint32_t kDataCmdCmdWrite = 0;
inline constexpr uint32_t kDataCmdCmdRead = 1;

inline constexpr uint32_t kDataCmdDat = 0;

class IntelI2cController;
using IntelI2cControllerType = ddk::Device<IntelI2cController, ddk::Initializable, ddk::Unbindable>;

class IntelI2cController : public IntelI2cControllerType,
                           public ddk::I2cImplProtocol<IntelI2cController, ddk::base_protocol> {
 public:
  IntelI2cController(zx_device_t* parent) : IntelI2cControllerType(parent), pci_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkInit(ddk::InitTxn txn);

  zx_status_t Reset() TA_REQ(mutex_);
  zx_status_t WaitForRxFull(const zx::time deadline);
  zx_status_t WaitForTxEmpty(const zx::time deadline);
  zx_status_t WaitForStopDetect(const zx::time deadline);
  zx_status_t ClearStopDetect();
  zx_status_t CheckForError();

  // Acts on the DATA_CMD register, and clear
  // interrupt masks as appropriate
  zx_status_t IssueRx(const uint32_t data_cmd);
  zx_status_t FlushRxFullIrq();
  uint8_t ReadRx();
  zx_status_t IssueTx(const uint32_t data_cmd);

  void Enable();
  uint8_t GetRxFifoDepth() const { return rx_fifo_depth_; }
  zx_status_t SetRxFifoThreshold(const uint32_t threshold);
  uint32_t GetRxFifoLevel();
  bool IsRxFifoEmpty();
  bool IsTxFifoFull();
  bool IsBusIdle();
  uint32_t StopDetected();
  void SetAddressingMode(const uint32_t addr_mode_bit);
  void SetTargetAddress(const uint32_t addr_mode_bit, const uint32_t address);

  uint32_t I2cImplGetBusCount();
  zx_status_t I2cImplGetMaxTransferSize(const uint32_t bus_id, size_t* out_size);
  zx_status_t I2cImplSetBitrate(const uint32_t bus_id, const uint32_t bitrate);
  zx_status_t I2cImplTransact(const uint32_t bus_id, const i2c_impl_op_t* op_list,
                              const size_t op_count);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  zx_status_t Init();
  ddk::Pci pci_;

  thrd_t irq_thread_;
  zx::interrupt irq_handle_;
  zx::event event_handle_;
  int IrqThread();

  zx_status_t DeviceSpecificInit(const uint16_t device_id);

  zx_status_t ComputeBusTiming();

  uint32_t GetRxFifoThreshold();
  uint32_t GetTxFifoThreshold();
  zx_status_t SetTxFifoThreshold(const uint32_t threshold);

  zx_status_t SetBusFrequency(const uint32_t frequency);

  zx_status_t AddSubordinates();
  zx_status_t AddSubordinate(const uint8_t width, const uint16_t address,
                             const zx_device_prop_t* props, const uint32_t propcount);

  static uint32_t ComputeSclHcnt(const uint32_t controller_freq, const uint32_t t_high_nanos,
                                 const uint32_t t_r_nanos);

  static uint32_t ComputeSclLcnt(const uint32_t controller_freq, const uint32_t t_low_nanos,
                                 const uint32_t t_f_nanos);

  static uint8_t ExtractTxFifoDepthFromParam(const uint32_t param);
  static uint8_t ExtractRxFifoDepthFromParam(const uint32_t param);
  static uint32_t ChipAddrMask(const int width);

  MMIO_PTR I2cRegs* regs_;
  MMIO_PTR volatile uint32_t* soft_reset_;

  mtx_t mutex_;
  uint8_t rx_fifo_depth_;

  std::optional<ddk::MmioBuffer> mmio_;

  uint32_t controller_freq_;
  uint32_t bus_freq_;

  // Bus parameters
  uint16_t sda_hold_;
  // Standard speed parameters
  uint16_t ss_scl_hcnt_;
  uint16_t ss_scl_lcnt_;
  // Fast mode speed parameters
  uint16_t fs_scl_hcnt_;
  uint16_t fs_scl_lcnt_;
  // Fast mode plus speed parameters
  uint16_t fmp_scl_hcnt_;
  uint16_t fmp_scl_lcnt_;

  uint8_t tx_fifo_depth_;

  std::map<uint16_t, std::unique_ptr<IntelI2cSubordinate>> subordinates_ TA_GUARDED(mutex_);

  mtx_t irq_mask_mutex_;
};

}  // namespace intel_i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_INTEL_I2C_INTEL_I2C_CONTROLLER_H_

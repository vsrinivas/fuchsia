// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_

#include <zircon/assert.h>

#include <hwreg/bitfields.h>

namespace tgl_registers {

enum Ddi {
  DDI_A = 0,
  DDI_B,
  DDI_C,
  DDI_D,
  DDI_E,

  DDI_TC_1 = DDI_D,
  DDI_TC_2,
  DDI_TC_3,
  DDI_TC_4,
  DDI_TC_5,
  DDI_TC_6,
};

// Interrupt registers for the south (in the PCH) display engine.
//
// SINTERRUPT is made up of the interrupt registers below.
// - ISR (Interrupt Status Register), also abbreviated to SDE_ISR
// - IMR (Interrupt Mask Register), also abbreviated to SDE_IMR
// - IIR (Interrupt Identity Register), also abbreviated to SDE_IIR
// - IER (Interrupt Enable Register), also abbreviated to SDE_IER
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 820-821
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 800-801
//
// The individual bits in each register are covered in the South Display Engine
// Interrupt Bit Definition, or SDE_INTERRUPT.
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1328-1329
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 874-875
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 854-855
class SdeInterruptBase : public hwreg::RegisterBase<SdeInterruptBase, uint32_t> {
 public:
  // SDE_INTERRUPT documents the base MMIO offset. SINTERRUPT documents the
  // individual register offsets.
  static constexpr uint32_t kSdeIntMask = 0xc4004;
  static constexpr uint32_t kSdeIntIdentity = 0xc4008;
  static constexpr uint32_t kSdeIntEnable = 0xc400c;

  hwreg::BitfieldRef<uint32_t> skl_ddi_bit(Ddi ddi) {
    uint32_t bit;
    switch (ddi) {
      case DDI_A:
        bit = 24;
        break;
      case DDI_B:
      case DDI_C:
      case DDI_D:
        bit = 20 + ddi;
        break;
      case DDI_E:
        bit = 25;
        break;
      default:
        bit = -1;
    }
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> icl_ddi_bit(Ddi ddi) {
    uint32_t bit;
    switch (ddi) {
      case DDI_A:
      case DDI_B:
      case DDI_C:
        bit = 16 + ddi - DDI_A;
        break;
      case DDI_TC_1:
      case DDI_TC_2:
      case DDI_TC_3:
      case DDI_TC_4:
      case DDI_TC_5:
      case DDI_TC_6:
        bit = 24 + ddi - DDI_TC_1;
        break;
      default:
        bit = -1;
    }
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<SdeInterruptBase>(offset); }
};

// DE_HPD_INTERRUPT : Display Engine HPD Interrupts for Type C / Thunderbolt (since gen11)
class HpdInterruptBase : public hwreg::RegisterBase<HpdInterruptBase, uint32_t> {
 public:
  static constexpr uint32_t kHpdIntMask = 0x44474;
  static constexpr uint32_t kHpdIntIdentity = 0x44478;
  static constexpr uint32_t kHpdIntEnable = 0x4447c;

  hwreg::BitfieldRef<uint32_t> tc_hotplug(Ddi ddi) {
    ZX_DEBUG_ASSERT(ddi >= DDI_TC_1 && ddi <= DDI_TC_6);
    uint32_t bit = 16 + ddi - DDI_TC_1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> tbt_hotplug(Ddi ddi) {
    ZX_DEBUG_ASSERT(ddi >= DDI_TC_1 && ddi <= DDI_TC_6);
    uint32_t bit = ddi - DDI_TC_1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<HpdInterruptBase>(offset); }
};

// TBT_HOTPLUG_CTL : Thunderbolt Hot Plug Control (since gen11)
class TbtHotplugCtrl : public hwreg::RegisterBase<TbtHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<TbtHotplugCtrl>(kOffset); }

 private:
  static constexpr uint32_t kOffset = 0x44030;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_to_first_bit(Ddi ddi) {
    switch (ddi) {
      case DDI_A:
      case DDI_B:
      case DDI_C:
        ZX_DEBUG_ASSERT_MSG(false, "Use south display hot plug registers for DDI A-C");
        return -1;
      case DDI_TC_1:
      case DDI_TC_2:
      case DDI_TC_3:
      case DDI_TC_4:
      case DDI_TC_5:
      case DDI_TC_6:
        return 4 * (ddi - DDI_TC_1);
    }
  }
};

// TC_HOTPLUG_CTL : Type-C Hot Plug Control (since gen11)
class TcHotplugCtrl : public hwreg::RegisterBase<TcHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<TcHotplugCtrl>(kOffset); }

 private:
  static constexpr uint32_t kOffset = 0x44038;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_to_first_bit(Ddi ddi) {
    switch (ddi) {
      case DDI_A:
      case DDI_B:
      case DDI_C:
        ZX_DEBUG_ASSERT_MSG(false, "Use south display hot plug registers for DDI A-C");
        return -1;
      case DDI_TC_1:
      case DDI_TC_2:
      case DDI_TC_3:
      case DDI_TC_4:
      case DDI_TC_5:
      case DDI_TC_6:
        return 4 * (ddi - DDI_TC_1);
    }
  }
};

// SHOTPLUG_CTL_DDI + SHOTPLUG_CTL_TC
class IclSouthHotplugCtrl : public hwreg::RegisterBase<IclSouthHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(Ddi ddi) {
    return hwreg::RegisterAddr<IclSouthHotplugCtrl>(ddi >= DDI_TC_1 ? kTcOffset : kDdiOffset);
  }

 private:
  static constexpr uint32_t kDdiOffset = 0xc4030;
  static constexpr uint32_t kTcOffset = 0xc4034;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_to_first_bit(Ddi ddi) {
    switch (ddi) {
      case DDI_A:
      case DDI_B:
      case DDI_C:
        return 4 * (ddi - DDI_A);  // SHOTPLUG_CTL_DDI
      case DDI_TC_1:
      case DDI_TC_2:
      case DDI_TC_3:
      case DDI_TC_4:
      case DDI_TC_5:
      case DDI_TC_6:
        return 4 * (ddi - DDI_TC_1);  // SHOTPLUG_CTL_TC
      default:
        return -1;
    }
  }
};

// SHOTPLUG_CTL + SHOTPLUG_CTL2
class SouthHotplugCtrl : public hwreg::RegisterBase<SouthHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(Ddi ddi) {
    uint32_t bit = ddi_to_first_bit(ddi) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(Ddi ddi) {
    return hwreg::RegisterAddr<SouthHotplugCtrl>(ddi == DDI_E ? kOffset2 : kOffset);
  }

 private:
  static constexpr uint32_t kOffset = 0xc4030;
  static constexpr uint32_t kOffset2 = 0xc403c;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 4;

  static uint32_t ddi_to_first_bit(Ddi ddi) {
    switch (ddi) {
      case DDI_A:
        return 24;
      case DDI_B:
      case DDI_C:
      case DDI_D:
        return 8 * (ddi - 1);
      case DDI_E:
        return 0;
      default:
        return -1;
    }
  }
};

// SFUSE_STRAP (South / PCH Fuses and Straps)
//
// This register is not documented on DG1.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1185
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 811
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 791
class PchDisplayFuses : public hwreg::RegisterBase<PchDisplayFuses, uint32_t> {
 public:
  // On Tiger Lake, indicates whether RawClk should be clocked at 24MHz or
  // 19.2MHz.
  DEF_BIT(8, rawclk_is_24mhz);

  // Not present (set to zero) on Tiger Lake. The driver is expected to use the
  // VBT (Video BIOS Table) or hotplug detection to figure out which ports are
  // present.
  DEF_BIT(2, port_b_present);
  DEF_BIT(1, port_c_present);
  DEF_BIT(0, port_d_present);

  static auto Get() { return hwreg::RegisterAddr<PchDisplayFuses>(0xc2014); }
};

// DDI_BUF_CTL
class DdiBufControl : public hwreg::RegisterBase<DdiBufControl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x64000;

  DEF_BIT(31, ddi_buffer_enable);
  DEF_FIELD(27, 24, dp_vswing_emp_sel);
  DEF_BIT(16, port_reversal);
  DEF_BIT(7, ddi_idle_status);
  DEF_BIT(4, ddi_a_lane_capability_control);
  DEF_FIELD(3, 1, dp_port_width_selection);
  DEF_BIT(0, init_display_detected);
};

// High byte of DDI_BUF_TRANS
class DdiBufTransHi : public hwreg::RegisterBase<DdiBufTransHi, uint32_t> {
 public:
  DEF_FIELD(20, 16, vref);
  DEF_FIELD(10, 0, vswing);
};

// Low byte of DDI_BUF_TRANS
class DdiBufTransLo : public hwreg::RegisterBase<DdiBufTransLo, uint32_t> {
 public:
  DEF_BIT(31, balance_leg_enable);
  DEF_FIELD(17, 0, deemphasis_level);
};

// DISPIO_CR_TX_BMU_CR0
class DisplayIoCtrlRegTxBmu : public hwreg::RegisterBase<DisplayIoCtrlRegTxBmu, uint32_t> {
 public:
  DEF_FIELD(27, 23, disable_balance_leg);

  hwreg::BitfieldRef<uint32_t> tx_balance_leg_select(Ddi ddi) {
    int bit = 8 + 3 * ddi;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 2, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<DisplayIoCtrlRegTxBmu>(0x6c00c); }
};

// DDI_AUX_CTL (DDI AUX Channel Control)
//
// Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 342-345
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 321-323
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 436-438
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 432-434
class DdiAuxControl : public hwreg::RegisterBase<DdiAuxControl, uint32_t> {
 public:
  // True while the DDI is performing an AUX transaction.
  //
  // The driver sets this field to true to start an AUX transaction. The
  // hardware resets it back to false when the AUX transaction is completed.
  //
  // The register should not be modified while this field is true.
  //
  // On Kaby Lake and Skylake, DDI_AUX_MUTEX must be acquired before setting up
  // an AUX transaction.
  DEF_BIT(31, transaction_in_progress);

  // Set to true by hardware when it completes an AUX transaction.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(30, transaction_done);

  // If true, an interrupt is triggered when an AUX transaction is completed.
  DEF_BIT(29, interrupt_on_done);

  // Set to true by hardware when an AUX transaction times out.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(28, timeout);

  // Selects the AUX transaction timeout.
  //
  // The AUX transaction limit in the DisplayPort specification is 500us.
  //
  // The values are documented as 0 (400us, unsupported), 1 (600us), 2 (800us)
  DEF_FIELD(27, 26, timeout_timer_select);

  // Only documented on Kaby Lake and Skylake. The docs advise against using.
  static constexpr int kTimeoutUnsupported400us = 0;

  static constexpr int kTimeout600us = 1;
  static constexpr int kTimeout800us = 2;

  // 4,000 us on Tiger Lake and DG1. 1,600 us on Kaby Lake and Skylake.
  static constexpr int kTimeoutLarge = 3;

  // Set to true by hardware when an AUX transaction receives invalid data.
  //
  // The received data could be invalid due to: corruption detected, the bits
  // received don't add up to an integer number of bytes, more than 20 bytes
  // received.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(25, receive_error);

  // Total number of bytes in an AUX message, including the message header.
  //
  // The driver writes this field to indicate the message size for the next AUX
  // transaction. The hardware writes this field to indicate the response size
  // for the last AUX transaction.
  //
  // The message includes the header bytes (4 for command, 2 for reply). The
  // DisplayPort specification states that the maximum data size is 16 bytes,
  // leading to a 20-byte maximum message size.
  //
  // The driver must write values between 1 and 20. The value read from this
  // field is only valid and meaningful if `transaction_done` is true, and
  // `transaction_in_progress`, `timeout`, and `receive_error` are false.
  DEF_FIELD(24, 20, message_size);

  // Directs AUX transactions to the Thunderbolt IO, or the USB-C / Combo IO.
  //
  // If true, transactions will be performed via the Thunderbolt controller.
  // Otherwise, the transactions will be performed over USB-C (using the FIA) or
  // over the Combo DDI IO.
  //
  // This field is reserved (must be false) on Kaby Lake and Skylake, which
  // don't support Thunderbolt IO.
  DEF_BIT(11, use_thunderbolt);

  // Number of SYNC pulses sent during SYNC for eDP fast wake transactions.
  //
  // The value is the number of SYNC pulses minus 1.
  DEF_FIELD(9, 5, fast_wake_sync_pulse_count);

  // `fast_wake_sync_pulse_count` should be set to 7, to match the Embedded
  // DisplayPort standard requirement for 8 pre-charge pulses (zeros) in the
  // AUX_PHY_WAKE preamble.
  static constexpr int kFastWakeSyncPulseCount = 8 - 1;

  // Number of SYNC pulses sent during SYNC for standard transactions.
  //
  // The value is the number of SYNC pulses minus 1. This is the sum of the
  // 10-16 pre-charge pulses (zeros) and the 16 consecutive zeros at the start
  // of the AUX_SYNC pattern.
  DEF_FIELD(4, 0, sync_pulse_count);

  // `sync_pulse_count` should be set to at least 25, to meet the DisplayPort
  // 26-pulse minimum, which is equivalent to 10 pre-charge pulses.
  static constexpr int kMinSyncPulseCount = 26 - 1;

  // For Kaby Lake and Skylake DDI A - DDI E.
  //
  // The Kaby Lake and Skylake references only document the AUX registers for
  // DDIs A-D. Other manuals, such as IHD-OS-ICLLP-Vol 2c-1.20, document AUX
  // registers for DDIs E-F, and their MMIO addresses are what we'd expect.
  // For now, we assume DDI E has an AUX channel that works like the other DDIs.
  static auto GetForKabyLakeDdi(Ddi ddi) {
    ZX_ASSERT(ddi >= Ddi::DDI_A);
    ZX_ASSERT(ddi <= Ddi::DDI_E);

    const int ddi_index = ddi - Ddi::DDI_A;
    return hwreg::RegisterAddr<DdiAuxControl>(0x64010 + 0x100 * ddi_index);
  }

  // For Tiger Lake and DG1.
  static auto GetForTigerLakeDdi(Ddi ddi) {
    ZX_ASSERT(ddi >= Ddi::DDI_A);
    ZX_ASSERT(ddi <= Ddi::DDI_TC_6);

    if (ddi < Ddi::DDI_TC_1) {
      // The addresses match the Kaby Lake and Skylake DDIs.
      const int ddi_index = ddi - Ddi::DDI_A;
      return hwreg::RegisterAddr<DdiAuxControl>(0x64010 + 0x100 * ddi_index);
    }
    // The addresses for Type C DDIs are right after the addresses for the Combo
    // DDIs. The compiler will optimize the if away.
    const int tc_ddi_index = ddi - Ddi::DDI_TC_1;
    return hwreg::RegisterAddr<DdiAuxControl>(0x64310 + 0x100 * tc_ddi_index);
  }
};

// DDI_AUX_DATA (DDI AUX Channel Data)
//
// Each DDI has 5 instances of the DDI_AUX_DATA register, making up a 20-byte
// buffer for storing AUX messages. The MMIO addresses for the 5 instances are
// consecutive.
//
// Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 346-351
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 324-330
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 439
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 435
class DdiAuxData : public hwreg::RegisterBase<DdiAuxData, uint32_t> {
 public:
  // The most significant byte in each 32-bit register gets transmitted first.
  // Intel machines are little-endian, so the transmission order doesn't match
  // the memory order. The `swapped_` part of the name aims to draw attention
  // to this subtlety.
  //
  // The value is not meaningful while the corresponding DDI_AUX_CTL register's
  // `transaction_in_progress` field is true.
  DEF_FIELD(31, 0, swapped_bytes);

  // DDI_AUX_DATA_*_0 for the AUX channel with the given control register.
  //
  // The DDI_AUX_DATA_*_[1-4] data registers are accessed using direct MMIO
  // operations.
  //
  // We can get away with using DDI_AUX_CTL as the input because all DDI AUX
  // channels currently have the same MMIO layout. When this isn't the case
  // anymore, we'll replace this factory function with GetFor*Ddi() functions,
  // matching DdiAuxControl.
  static auto GetData0ForAuxControl(const DdiAuxControl& aux_control) {
    static constexpr uint32_t kAuxControlMmioBase = 0x64010;
    static constexpr uint32_t kAuxDataMmioBase = 0x64014;
    return hwreg::RegisterAddr<DdiAuxData>(aux_control.reg_addr() +
                                           (kAuxDataMmioBase - kAuxControlMmioBase));
  }
};

// DDI_AUX_MUTEX (DDI AUX Channel Mutex)
//
// This register is not documented on Tiger Lake or DG1.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 440-441
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 436-437
class DdiAuxMutex : public hwreg::RegisterBase<DdiAuxMutex, uint32_t> {
 public:
  // If true, the mutex is used to arbitrate AUX channel access.
  //
  // The mutex must be enabled and acquired if PSR 1/2 (Panel Self-Refresh) or
  // GTC (Global Time Code) are used. Otherwise, the mutex can remain disabled.
  DEF_BIT(31, enabled);

  // Reads acquire the mutex, writes release the mutex.
  //
  // Any read is an attempt to acquire the mutex. A successful attempt returns
  // true in this field. Once the driver acquires the mutex, it retains
  // ownership (reads continue to return true) until it explicitly releases the
  // mutex.
  //
  // This is a Write-Clear field. Writing true releases the mutex.
  //
  // The driver should release the mutex once it completes an AUX transaction,
  // so the hardware can use it as well.
  DEF_BIT(30, acquired);

  // We can get away with using DDI_AUX_CTL as the input because all DDI AUX
  // channels currently have the same MMIO layout. When this isn't the case
  // anymore, we'll replace this factory function with GetFor*Ddi() functions,
  // matching DdiAuxControl.
  static auto GetForAuxControl(const DdiAuxControl& aux_control) {
    static constexpr uint32_t kAuxControlMmioBase = 0x64010;
    static constexpr uint32_t kAuxMutexMmioBase = 0x6402c;
    return hwreg::RegisterAddr<DdiAuxData>(aux_control.reg_addr() +
                                           (kAuxMutexMmioBase - kAuxControlMmioBase));
  }
};

// DP_TP_CTL
class DdiDpTransportControl : public hwreg::RegisterBase<DdiDpTransportControl, uint32_t> {
 public:
  static constexpr uint32_t kBaseAddr = 0x64040;

  DEF_BIT(31, transport_enable);
  DEF_BIT(27, transport_mode_select);
  DEF_BIT(25, force_act);
  DEF_BIT(18, enhanced_framing_enable);

  DEF_FIELD(10, 8, dp_link_training_pattern);
  static constexpr int kTrainingPattern1 = 0;
  static constexpr int kTrainingPattern2 = 1;
  static constexpr int kIdlePattern = 2;
  static constexpr int kSendPixelData = 3;

  DEF_BIT(6, alternate_sr_enable);
};

// An instance of DdiRegs represents the registers for a particular DDI.
class DdiRegs {
 public:
  DdiRegs(Ddi ddi) : ddi_number_((int)ddi) {}

  hwreg::RegisterAddr<tgl_registers::DdiBufControl> DdiBufControl() {
    return GetReg<tgl_registers::DdiBufControl>();
  }
  hwreg::RegisterAddr<tgl_registers::DdiDpTransportControl> DdiDpTransportControl() {
    return GetReg<tgl_registers::DdiDpTransportControl>();
  }
  hwreg::RegisterAddr<tgl_registers::DdiBufTransHi> DdiBufTransHi(int index) {
    return hwreg::RegisterAddr<tgl_registers::DdiBufTransHi>(0x64e00 + 0x60 * ddi_number_ +
                                                             8 * index + 4);
  }
  hwreg::RegisterAddr<tgl_registers::DdiBufTransLo> DdiBufTransLo(int index) {
    return hwreg::RegisterAddr<tgl_registers::DdiBufTransLo>(0x64e00 + 0x60 * ddi_number_ +
                                                             8 * index);
  }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg() {
    return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x100 * ddi_number_);
  }

  uint32_t ddi_number_;
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_

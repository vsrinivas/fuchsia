// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <lib/mmio/mmio.h>
#include <soc/mt8167/mt8167-hw.h>
#include <zircon/types.h>

namespace {
// There are 2 sets of GPIO Pull settings register banks, those under GPIO and those under IOCFG.
// Those under the GPIO have a consistent numbering mapping such that the register offsets
// can be calculated based on the GPIO number.  The GPIOs that fall into IOCFG are marked as '0'
// in kGpioPullInGpioRegs and return false in GpioPullEnReg/GpioPullSelReg methods to indicate that
// they are not supported in the GPIO registers so we then try IoConfigReg methods. Note that the
// last 3 GPIO numbers in the array don't fall under GPIO or IOCFG (as any other number past 127).
static constexpr bool kGpioPullInGpioRegs[][16] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},  // 0 (first GPIO in this line).
    {0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},  // 16.
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},  // 32.
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 48.
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1},  // 64.
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 80.
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},  // 96.
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0},  // 112 (first is 112, last is 127).
};

}  // namespace

namespace gpio {

enum PullAmount {
  kNoPull,
  kPull10K,
  kPull50K,
  kPull10K50K,
  kPull75K,
  kPull2K,
  kPull75K2K,
  kPull200K,
  kPull75K200K,
};

// GPIO MODE defines PINMUX for this device
class GpioModeReg : public hwreg::RegisterBase<GpioModeReg, uint16_t> {
 public:
  static constexpr uint16_t kModeGpio = 0;  // GPIO mode is always 0
  static constexpr uint64_t kModeMax = 8;   // 3 bits per mode

  static uint16_t GetMode(ddk::MmioBuffer* mmio, size_t idx) {
    return Read(mmio, idx).GetMode(idx % kItemsPerReg);
  }

  static void SetMode(ddk::MmioBuffer* mmio, size_t idx, uint16_t value) {
    Read(mmio, idx).SetMode(idx % kItemsPerReg, value).WriteTo(mmio);
  }

 private:
  static constexpr size_t kItemsPerReg = 5;

  // Registers are 16 bits, separated by 0x10 bytes, with kItemsPerReg values per register.
  static uint32_t Idx2Offset(size_t idx) {
    return static_cast<uint32_t>(0x300 + (idx / kItemsPerReg) * 0x10);
  }

  uint16_t GetMode(size_t offset) const {
    switch (offset) {
      case 0:
        return mode0();
      case 1:
        return mode1();
      case 2:
        return mode2();
      case 3:
        return mode3();
    }
    return mode4();
  }

  GpioModeReg& SetMode(size_t offset, uint16_t value) {
    switch (offset) {
      case 0:
        return set_mode0(value);
      case 1:
        return set_mode1(value);
      case 2:
        return set_mode2(value);
      case 3:
        return set_mode3(value);
    }
    return set_mode4(value);
  }

  static GpioModeReg Read(ddk::MmioBuffer* mmio, size_t idx) {
    return hwreg::RegisterAddr<GpioModeReg>(Idx2Offset(idx)).ReadFrom(mmio);
  }

  DEF_FIELD(14, 12, mode4);
  DEF_FIELD(11, 9, mode3);
  DEF_FIELD(8, 6, mode2);
  DEF_FIELD(5, 3, mode1);
  DEF_FIELD(2, 0, mode0);
};

class GpioBitFieldView : public ddk::MmioView {
  // Registers are 16 bits and separated by 0x10 bytes.
  uint32_t Idx2Offset(size_t idx) const { return static_cast<uint32_t>((idx / 16) * 0x10); }

 public:
  explicit GpioBitFieldView(ddk::MmioBuffer& mmio, zx_off_t offset, size_t size)
      : ddk::MmioView(mmio.View(offset, size)) {}

  void ModifyBit(size_t idx, bool val) const {
    ddk::MmioView::ModifyBit<uint16_t>(val, idx % 16, Idx2Offset(idx));
  }

  uint16_t GetBit(size_t idx) const {
    return ddk::MmioView::GetBit<uint16_t>(idx % 16, Idx2Offset(idx));
  }
};

class GpioDirReg : public GpioBitFieldView {
 public:
  explicit GpioDirReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 0, 0x100) {}
  void SetDir(size_t idx, bool is_out) const { ModifyBit(idx, is_out); }
};

class GpioOutReg : public GpioBitFieldView {
 public:
  explicit GpioOutReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 0x100, 0x100) {}
  void SetVal(size_t idx, bool val) const { ModifyBit(idx, val); }
};

class GpioInReg : public GpioBitFieldView {
 public:
  explicit GpioInReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 0x200, 0x100) {}
  uint16_t GetVal(size_t idx) const { return GetBit(idx); }
};

class GpioPullEnReg : public GpioBitFieldView {
 public:
  explicit GpioPullEnReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 0x500, 0x100) {}

  bool PullEnable(size_t idx) const { return PullEnableInternal(idx, true); }
  bool PullDisable(size_t idx) const { return PullEnableInternal(idx, false); }

 private:
  bool PullEnableInternal(size_t idx, bool val) const {
    if (idx >= sizeof(kGpioPullInGpioRegs) / (sizeof(**kGpioPullInGpioRegs)) ||
        !kGpioPullInGpioRegs[idx / 16][idx % 16]) {
      return false;
    }
    ModifyBit(idx, val);
    return true;
  }
};

class GpioPullSelReg : public GpioBitFieldView {
 public:
  explicit GpioPullSelReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 0x600, 0x100) {}
  bool SetPullUp(size_t idx) const { return SetPullInternal(idx, true); }
  bool SetPullDown(size_t idx) const { return SetPullInternal(idx, false); }

 private:
  bool SetPullInternal(size_t idx, bool up) const {
    if (idx >= sizeof(kGpioPullInGpioRegs) / (sizeof(**kGpioPullInGpioRegs)) ||
        !kGpioPullInGpioRegs[idx / 16][idx % 16]) {
      return false;
    }
    ModifyBit(idx, up);
    return true;
  }
};

class IoConfigReg : public ddk::MmioBuffer {
 public:
  explicit IoConfigReg(ddk::MmioBuffer mmio) : ddk::MmioBuffer(std::move(mmio)) {}
  bool SetPullUp(size_t idx) const { return SetPullInternal(idx, true); }
  bool SetPullDown(size_t idx) const { return SetPullInternal(idx, false); }
  bool PullEnable(size_t idx, PullAmount amount) const { return PullEnableInternal(idx, amount); }
  bool PullDisable(size_t idx) const { return PullEnableInternal(idx, kNoPull); }

 private:
  // This list pull settings not in the GPIO register set, but only here in IOCFG.
  static constexpr struct {
    size_t idx;
    uint32_t reg_offset;
    uint32_t up_down_bit;
    uint32_t pull_bit_start;
    PullAmount pull_amount_sets_reg_to_1;
    PullAmount pull_amount_sets_reg_to_2;
    PullAmount pull_amount_sets_reg_to_3;
  } pull_regs[] = {
      // clang-format off
        {  14, 0x550, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        {  15, 0x560,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        {  16, 0x560,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        {  17, 0x560, 10,  8, kPull10K,  kPull50K,    kPull10K50K},

        {  21, 0x560, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        {  22, 0x570,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        {  23, 0x570,  6,  4, kPull10K,  kPull50K,    kPull10K50K},

        {  40, 0x580,  2,  0, kPull75K,   kPull2K,     kPull75K2K},
        {  41, 0x580,  6,  4, kPull75K,   kPull2K,     kPull75K2K},
        {  42, 0x590,  2,  0, kPull75K, kPull200K,   kPull75K200K},
        {  43, 0x590,  6,  4, kPull75K, kPull200K,   kPull75K200K},

        {  68, 0x550, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        {  69, 0x550,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        {  70, 0x540,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        {  71, 0x540, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        {  72, 0x540, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        {  73, 0x550,  2,  0, kPull10K,  kPull50K,    kPull10K50K},

        { 104, 0x540,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        { 105, 0x530, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        { 106, 0x520, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        { 107, 0x530,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        { 108, 0x530,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        { 109, 0x530, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        { 110, 0x510, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        { 111, 0x510, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        { 112, 0x510,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        { 113, 0x510,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        { 114, 0x520, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        { 115, 0x520,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
        { 116, 0x520,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        { 117, 0x500, 14, 12, kPull10K,  kPull50K,    kPull10K50K},
        { 118, 0x500, 10,  8, kPull10K,  kPull50K,    kPull10K50K},
        { 119, 0x500,  6,  4, kPull10K,  kPull50K,    kPull10K50K},
        { 120, 0x500,  2,  0, kPull10K,  kPull50K,    kPull10K50K},
      // clang-format on
  };

  bool SetPullInternal(size_t idx, bool up) const {
    for (auto& i : pull_regs) {
      if (i.idx == idx) {
        ModifyBit<uint16_t>(!up, i.up_down_bit, i.reg_offset);
        return true;
      }
    }
    return false;
  }
  bool PullEnableInternal(size_t idx, PullAmount pull) const {
    for (auto& i : pull_regs) {
      if (i.idx == idx) {
        uint16_t val;
        if (pull == kNoPull) {
          val = 0;
        } else if (pull == i.pull_amount_sets_reg_to_1) {
          val = 1;
        } else if (pull == i.pull_amount_sets_reg_to_2) {
          val = 2;
        } else if (pull == i.pull_amount_sets_reg_to_3) {
          val = 3;
        } else {
          return false;  // Not supported pull amount for this GPIO.
        }
        ModifyBits<uint16_t>(val, i.pull_bit_start, 2, i.reg_offset);
        return true;
      }
    }
    return false;
  }
};

class ExtendedInterruptReg : public ddk::MmioBuffer {
 public:
  explicit ExtendedInterruptReg(ddk::MmioBuffer mmio) : ddk::MmioBuffer(std::move(mmio)) {}
  void Enable(size_t idx) const { EnableInternal(idx, true); }
  void Disable(size_t idx) const { EnableInternal(idx, false); }
  bool IsEnabled(size_t idx) const {
    return !GetBit<uint32_t>(idx % kBitsPerReg, 0x80 + (idx / kBitsPerReg) * kBytesRegSeparation);
  }
  void SetPolarity(size_t idx, bool high) const {
    SetBit<uint32_t>(idx % kBitsPerReg,
                     (high ? 0x340 : 0x380) + (idx / kBitsPerReg) * kBytesRegSeparation);
  }
  void SetEdge(size_t idx, bool edge) const {
    SetBit<uint32_t>(idx % kBitsPerReg,
                     (edge ? 0x1C0 : 0x180) + (idx / kBitsPerReg) * kBytesRegSeparation);
  }

  void SetDomain0(size_t idx) const {
    // These registers are not described on the reference manual.
    SetBit<uint32_t>(idx % kBitsPerReg, 0x400 + (idx / kBitsPerReg) * kBytesRegSeparation);
  }
  void AckInterrupt(size_t idx) const {
    // These registers are not described on the reference manual.
    SetBit<uint32_t>(idx % kBitsPerReg, 0x040 + (idx / kBitsPerReg) * kBytesRegSeparation);
  }
  uint32_t GetNextInterrupt(uint32_t start) const {
    for (uint32_t i = start; i < MT8167_GPIO_EINT_MAX; i += kBitsPerReg) {
      // These registers are not described on the reference manual.
      auto reg = Read<uint32_t>(0x000 + (i / kBitsPerReg) * kBytesRegSeparation);
      if (reg) {
        return i + 31 - __builtin_clz(reg);
      }
    }
    return kInvalidInterruptIdx;
  }

  static constexpr uint32_t kInvalidInterruptIdx = static_cast<uint32_t>(-1);

 private:
  void EnableInternal(size_t idx, bool enable) const {
    SetBit<uint32_t>(idx % kBitsPerReg,
                     (enable ? 0x100 : 0xC0) + (idx / kBitsPerReg) * kBytesRegSeparation);
  }
  static constexpr uint32_t kBitsPerReg = 32;
  static constexpr uint32_t kBytesRegSeparation = 4;
};
}  // namespace gpio

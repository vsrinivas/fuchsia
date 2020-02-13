// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace gpio {

constexpr uint32_t kGpioMax = 142;

// GPIO Cfg defines PINMUX for this device
class GpioCfgReg : public hwreg::RegisterBase<GpioCfgReg, uint32_t> {
 public:
  static constexpr uint32_t kModeGpio = 0;  // GPIO mode is always 0
  static constexpr uint64_t kModeMax = 16;  // 4 bits per mode

  static uint32_t GetMode(ddk::MmioBuffer* mmio, size_t idx) { return Read(mmio, idx).FUNC_SEL(); }

  static void SetMode(ddk::MmioBuffer* mmio, size_t idx, uint32_t value) {
    Read(mmio, idx).set_FUNC_SEL(value).WriteTo(mmio);
  }
  static void SetOut(ddk::MmioBuffer* mmio, size_t idx, bool output) {
    Read(mmio, idx).set_GPIO_OE(static_cast<uint32_t>(output)).WriteTo(mmio);
  }

  static void SetPullNone(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 0); }
  static void SetPullDown(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 1); }
  static void SetPullUp(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 3); }
  static void SetStrength(ddk::MmioBuffer* mmio, size_t idx, uint8_t mA) {
    Read(mmio, idx).set_DRV_STRENGTH(mA / 2 - 1).WriteTo(mmio);
  }

 protected:
  // Registers are separated by 0x1000 bytes.
  static uint32_t Idx2Offset(size_t idx) { return static_cast<uint32_t>(idx * 0x1000); }

 private:
  static GpioCfgReg Read(ddk::MmioBuffer* mmio, size_t idx) {
    return hwreg::RegisterAddr<GpioCfgReg>(Idx2Offset(idx)).ReadFrom(mmio);
  }
  static void SetPull(ddk::MmioBuffer* mmio, size_t idx, uint32_t reg_value) {
    Read(mmio, idx).set_GPIO_PULL(reg_value).WriteTo(mmio);
  }

  DEF_BIT(10, GPIO_HIHYS_EN);
  DEF_BIT(9, GPIO_OE);
  DEF_FIELD(8, 6, DRV_STRENGTH);
  DEF_FIELD(5, 2, FUNC_SEL);
  DEF_FIELD(1, 0, GPIO_PULL);
};

class GpioBitFieldView : public ddk::MmioView {
 public:
  explicit GpioBitFieldView(ddk::MmioBuffer& mmio, zx_off_t offset, size_t size)
      : ddk::MmioView(mmio.View(offset, size)) {}

 protected:
  // Registers are separated by 0x1000 bytes.
  uint32_t Idx2Offset(size_t idx) const { return static_cast<uint32_t>(idx * 0x1000); }
};

class GpioInOutReg : public GpioBitFieldView {
 public:
  explicit GpioInOutReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 4, 0x1000 * kGpioMax) {}
  bool GetVal(size_t idx) const {
    return static_cast<bool>(ddk::MmioView::GetBit<uint32_t>(0, Idx2Offset(idx)));
  }
  void SetVal(size_t idx, bool val) const {
    ModifyBit<uint32_t>(static_cast<uint32_t>(val), 1, Idx2Offset(idx));
  }
};

enum class Mode { EdgeLow, EdgeHigh, EdgeDual, LevelLow, LevelHigh };

class GpioIntCfgReg : public GpioBitFieldView {
 public:
  explicit GpioIntCfgReg(ddk::MmioBuffer& mmio) : GpioBitFieldView(mmio, 8, 0x1000 * kGpioMax) {}

  void EnableCombined(size_t idx, bool val) const {
    uint32_t target_proc = 0x7;  // NONE. Don't route to any processor subsystem.
    if (val) {
      target_proc = 0x4;  // APPS. Route the GPIO[n] signal to APSS summary interrupt.
    }
    ModifyBit<uint32_t>(static_cast<uint32_t>(val), 4, Idx2Offset(idx));  // Raw status.
    ModifyBits<uint32_t>(target_proc, 5, 3, Idx2Offset(idx));
    ModifyBit<uint32_t>(static_cast<uint32_t>(val), 0, Idx2Offset(idx));  // Enable.
  }
  void EnableDirect(size_t idx, bool val) const {
    ModifyBit<uint32_t>(static_cast<uint32_t>(val), 8, Idx2Offset(idx));
  }
  void SetMode(size_t idx, Mode mode) const {
    uint32_t detect_reg = 0;
    uint32_t polarity_reg = 0;
    // clang-format off
        switch (mode) {
        case Mode::EdgeHigh:  detect_reg = 0x1; polarity_reg = 1; break;
        case Mode::EdgeLow:   detect_reg = 0x2; polarity_reg = 1; break;
        case Mode::EdgeDual:  detect_reg = 0x3; break; // polarity_reg is don't care.
        case Mode::LevelHigh: detect_reg = 0x0; polarity_reg = 1; break;
        case Mode::LevelLow:  detect_reg = 0x0; polarity_reg = 0; break;
        }
    // clang-format on
    ModifyBits<uint32_t>(detect_reg, 2, 2, Idx2Offset(idx));
    ModifyBit<uint32_t>(polarity_reg, 1, Idx2Offset(idx));
  }
  void SetPolarity(size_t idx, bool high) const {
    uint32_t detect_reg = GetBits<uint32_t>(2, 2, Idx2Offset(idx));
    uint32_t polarity_reg = GetBit<uint32_t>(1, Idx2Offset(idx));
    Mode old_mode;
    // clang-format off
        switch (detect_reg) {
        case 0x1: old_mode = Mode::EdgeHigh;                                  break;
        case 0x2: old_mode = Mode::EdgeLow;                                   break;
        case 0x3: old_mode = Mode::EdgeDual;                                  break;
        case 0x0: old_mode = polarity_reg ? Mode::LevelHigh : Mode::LevelLow; break;
        }
        switch (old_mode) {
        case Mode::EdgeHigh:  SetMode(idx, high ? Mode::EdgeHigh : Mode::EdgeLow);   break;
        case Mode::EdgeLow:   SetMode(idx, high ? Mode::EdgeHigh : Mode::EdgeLow);   break;
        case Mode::EdgeDual:                                                         break; // noop.
        case Mode::LevelHigh: SetMode(idx, high ? Mode::LevelHigh : Mode::LevelLow); break;
        case Mode::LevelLow:  SetMode(idx, high ? Mode::LevelHigh : Mode::LevelLow); break;
        }
    // clang-format on
  }
};

class TlmmDirConnIntReg : public GpioBitFieldView {
 protected:
  // Registers are separated by 4 bytes.
  uint32_t Idx2Offset(size_t idx) const { return static_cast<uint32_t>(idx * 4); }

 public:
  explicit TlmmDirConnIntReg(ddk::MmioBuffer& mmio)
      : GpioBitFieldView(mmio, 0x102000, 4 * kGpioMax) {}
  void Enable(size_t gpio_n, size_t dir_int_n) const {
    ModifyBits<uint32_t>(static_cast<uint32_t>(gpio_n), 0, 8, Idx2Offset(dir_int_n));
  }
};

class TlmmGpioIntrStatusReg : public GpioBitFieldView {
 public:
  explicit TlmmGpioIntrStatusReg(ddk::MmioBuffer& mmio)
      : GpioBitFieldView(mmio, 0xC, 0x1000 * kGpioMax) {}
  bool Status(size_t gpio_n) const {
    return static_cast<bool>(GetBit<uint32_t>(0, Idx2Offset(gpio_n)));
  }
  void Clear(size_t gpio_n) const { ClearBit<uint32_t>(0, Idx2Offset(gpio_n)); }
};

}  // namespace gpio

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <soc/msm8x53/msm8x53-hw.h>
#include <zircon/types.h>

namespace gpio {

// GPIO Cfg defines PINMUX for this device
class GpioCfgReg : public hwreg::RegisterBase<GpioCfgReg, uint32_t> {
public:
    static constexpr uint32_t kModeGpio = 0; // GPIO mode is always 0
    static constexpr uint64_t kModeMax = 16; // 4 bits per mode

    static uint32_t GetMode(ddk::MmioBuffer* mmio, size_t idx) {
        return Read(mmio, idx).FUNC_SEL();
    }

    static void SetMode(ddk::MmioBuffer* mmio, size_t idx, uint32_t value) {
        Read(mmio, idx).set_FUNC_SEL(value).WriteTo(mmio);
    }
    static void SetOut(ddk::MmioBuffer* mmio, size_t idx, bool output) {
        Read(mmio, idx).set_GPIO_OE(static_cast<uint32_t>(output)).WriteTo(mmio);
    }

    static void SetPullNone(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 0); }
    static void SetPullDown(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 1); }
    static void SetPullUp(ddk::MmioBuffer* mmio, size_t idx) { SetPull(mmio, idx, 3); }

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
    explicit GpioBitFieldView(mmio_buffer_t& mmio, zx_off_t offset, size_t size)
        : ddk::MmioView(mmio, offset, size) {}

    uint32_t GetBit(size_t idx) const {
        return ddk::MmioView::GetBit<uint32_t>(0, Idx2Offset(idx));
    }

protected:
    // Registers are separated by 0x1000 bytes.
    uint32_t Idx2Offset(size_t idx) const { return static_cast<uint32_t>(idx * 0x1000); }
};

class GpioInReg : public GpioBitFieldView {
public:
    explicit GpioInReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 4, 0x1000 * kMsm9x53GpioMax) {}
    bool GetVal(size_t idx) const { return static_cast<bool>(GetBit(idx)); }
};

class GpioOutReg : public GpioBitFieldView {
public:
    explicit GpioOutReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 8, 0x1000 * kMsm9x53GpioMax) {}
    void SetVal(size_t idx, bool val) const {
        ModifyBit<uint32_t>(static_cast<uint32_t>(val), 1, Idx2Offset(idx));
    }
};

} // namespace gpio

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/mmio.h>

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#include <zircon/types.h>

namespace gpio {

constexpr bool kGpioPullValid[][16] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0},
};

// GPIO MODE defines PINMUX for this device
class GpioModeReg : public hwreg::RegisterBase<GpioModeReg, uint16_t> {
public:
    static constexpr uint16_t kModeGpio = 0; // GPIO mode is always 0
    static constexpr uint64_t kModeMax = 8;  // 3 bits per mode

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
    explicit GpioBitFieldView(mmio_buffer_t& mmio, zx_off_t offset, size_t size)
        : ddk::MmioView(mmio, offset, size) {}

    void ModifyBit(size_t idx, bool val) const {
        ddk::MmioView::ModifyBit<uint16_t>(val, idx % 16, Idx2Offset(idx));
    }

    uint16_t GetBit(size_t idx) const {
        return ddk::MmioView::GetBit<uint16_t>(idx % 16, Idx2Offset(idx));
    }
};

class GpioDirReg : public GpioBitFieldView {
public:
    explicit GpioDirReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 0, 0x100) {}
    void SetDir(size_t idx, bool is_out) const { ModifyBit(idx, is_out); }
};

class GpioOutReg : public GpioBitFieldView {
public:
    explicit GpioOutReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 0x100, 0x100) {}
    void SetVal(size_t idx, bool val) const { ModifyBit(idx, val); }
};

class GpioInReg : public GpioBitFieldView {
public:
    explicit GpioInReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 0x200, 0x100) {}
    uint16_t GetVal(size_t idx) const { return GetBit(idx); }
};

class GpioPullEnReg : public GpioBitFieldView {
public:
    explicit GpioPullEnReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 0x500, 0x100) {}

    bool PullEnable(size_t idx) const { return PullEnableInternal(idx, true); }
    bool PullDisable(size_t idx) const { return PullEnableInternal(idx, false); }

private:
    bool PullEnableInternal(size_t idx, bool val) const {
        if (idx >= countof(kGpioPullValid) || !kGpioPullValid[idx / 16][idx % 16]) {
            return false;
        }
        ModifyBit(idx, val);
        return true;
    }
};

class GpioPullSelReg : public GpioBitFieldView {
public:
    explicit GpioPullSelReg(mmio_buffer_t& mmio)
        : GpioBitFieldView(mmio, 0x600, 0x100) {}
    bool SetPullUp(size_t idx) const { return SetPullInternal(idx, true); }
    bool SetPullDown(size_t idx) const { return SetPullInternal(idx, false); }

private:
    bool SetPullInternal(size_t idx, bool up) const {
        if (idx >= countof(kGpioPullValid) || !kGpioPullValid[idx / 16][idx % 16]) {
            return false;
        }
        ModifyBit(idx, up);
        return true;
    }
};

class IoConfigReg : public ddk::MmioBuffer {
public:
    explicit IoConfigReg(mmio_buffer_t& mmio)
        : ddk::MmioBuffer(mmio) {}
    bool SetPullUp(size_t idx) const { return SetPullInternal(idx, true); }
    bool SetPullDown(size_t idx) const { return SetPullInternal(idx, false); }
    bool PullEnable(size_t idx) const { return PullEnableInternal(idx, true); }
    bool PullDisable(size_t idx) const { return PullEnableInternal(idx, false); }

private:
    bool SetPullInternal(size_t idx, bool up) const {
        switch (idx) {
        case 40:
            ModifyBit<uint32_t>(!up, 2, 0x580);
            return true;
        case 41:
            ModifyBit<uint32_t>(!up, 6, 0x580);
            return true;
        case 42:
            ModifyBit<uint32_t>(!up, 2, 0x590);
            return true;
        case 43:
            ModifyBit<uint32_t>(!up, 6, 0x590);
            return true;
        }
        return false;
    }
    bool PullEnableInternal(size_t idx, bool enable) const {
        constexpr uint32_t r75K = 1;
        switch (idx) {
        case 40:
            ModifyBits(enable ? r75K : 0, 0, 2, 0x580);
            return true;
        case 41:
            ModifyBits(enable ? r75K : 0, 4, 2, 0x580);
            return true;
        case 42:
            ModifyBits(enable ? r75K : 0, 0, 2, 0x590);
            return true;
        case 43:
            ModifyBits(enable ? r75K : 0, 4, 2, 0x590);
            return true;
        }
        return false;
    }
};

class ExtendedInterruptReg : public ddk::MmioBuffer {
public:
    explicit ExtendedInterruptReg(mmio_buffer_t& mmio)
        : ddk::MmioBuffer(mmio) {}
    void Enable(size_t idx) const { EnableInternal(idx, true); }
    void Disable(size_t idx) const { EnableInternal(idx, false); }
    bool IsEnabled(size_t idx) const {
        return !GetBit<uint32_t>(
            idx % kBitsPerReg, 0x80 + (idx / kBitsPerReg) * kBytesRegSeparation);
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
            auto reg = Read<uint32_t>(
                0x000 + (i / kBitsPerReg) * kBytesRegSeparation);
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
} // namespace gpio

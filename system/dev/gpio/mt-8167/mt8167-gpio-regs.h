// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/mmio.h>

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#include <zircon/types.h>

namespace gpio {

// GPIO MODE defines PINMUX for this device
class GpioModeReg : public hwreg::RegisterBase<GpioModeReg, uint16_t> {
public:
    static constexpr uint64_t kModeMax = 8; // 3 bits per mode

    static uint16_t GetMode(ddk::MmioBuffer* mmio, size_t idx) {
        return Read(mmio, idx).GetMode(idx % kItemsPerReg);
    }

    static void SetMode(ddk::MmioBuffer* mmio, size_t idx, uint16_t value) {
        Read(mmio, idx).SetMode(idx % kItemsPerReg, value).WriteTo(mmio);
    }

private:
    static constexpr uint32_t kItemsPerReg = 5;

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

    uint16_t GetBit(size_t idx)  const {
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

} // namespace gpio

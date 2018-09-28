// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#include <zircon/types.h>

namespace gpio {

// GPIO MODE defines PINMUX for this device
class GpioModeReg : public hwreg::RegisterBase<GpioModeReg, uint16_t> {
private:
    static constexpr uint32_t kItemsPerReg = 5;

    // Registers are 16 bits, separated by 0x10 bytes, with kItemsPerReg values per register.
    static uint32_t Idx2Offset(int idx) { return 0x300 + (idx / kItemsPerReg) * 0x10; }

    uint16_t GetMode(int offset) {
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

    auto SetMode(int offset, uint16_t value) {
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

    static GpioModeReg Read(void* virt, int idx) {
        hwreg::RegisterIo reg_io(virt);
        return hwreg::RegisterAddr<GpioModeReg>(Idx2Offset(idx)).ReadFrom(&reg_io);
    }

    DEF_FIELD(14, 12, mode4);
    DEF_FIELD(11, 9, mode3);
    DEF_FIELD(8, 6, mode2);
    DEF_FIELD(5, 3, mode1);
    DEF_FIELD(2, 0, mode0);

public:
    static uint64_t GetModeMax() { return 8; } // 3 bits per mode

    static auto GetMode(void* virt, int idx) {
        return Read(virt, idx).GetMode(idx % kItemsPerReg);
    }

    static void SetMode(void* virt, int idx, uint16_t value) {
        hwreg::RegisterIo reg_io(virt);
        Read(virt, idx).SetMode(idx % kItemsPerReg, value).WriteTo(&reg_io);
    }
};

// TODO(andresoportus): Use bit manipulation lib when available.  Reconsider hwreg usage.
class GpioBitFieldReg : public hwreg::RegisterBase<GpioBitFieldReg, uint16_t> {
private:
    // Registers are 16 bits and separated by 0x10 bytes.
    static uint32_t Idx2Offset(int idx) { return (idx / 16) * 0x10; }

    bool GetBit(int offset) {
        return (all() >> offset) & 1;
    }

    auto SetBit(int offset, bool val) {
        if (val) {
            return set_all(static_cast<uint16_t>(all() | (static_cast<uint16_t>(1) << offset)));
        } else {
            return set_all(static_cast<uint16_t>(all() & ~(static_cast<uint16_t>(1) << offset)));
        }
    }

    static GpioBitFieldReg Read(void* virt, size_t offset, int idx) {
        // We get one offset from the caller and we add another offset via Idx2Offset below.
        void* p = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(virt) + offset);
        hwreg::RegisterIo reg_io(p);
        return hwreg::RegisterAddr<GpioBitFieldReg>(Idx2Offset(idx)).ReadFrom(&reg_io);
    }

    DEF_FIELD(15, 0, all);

public:
    static auto GetBit(void* virt, size_t offset, int idx) {
        return Read(virt, offset, idx).GetBit(idx % 16);
    }

    static void SetBit(void* virt, size_t offset, int idx, bool val) {
        void* p = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(virt) + offset);
        hwreg::RegisterIo reg_io(p);
        Read(virt, offset, idx).SetBit(idx % 16, val).WriteTo(&reg_io);
    }
};

class GpioDirReg : public GpioBitFieldReg {
public:
    static void SetDir(void* virt, int idx, bool is_out) { SetBit(virt, 0x0, idx, is_out); }

private:
};

class GpioOutReg : public GpioBitFieldReg {
public:
    static void SetVal(void* virt, int idx, bool val) { SetBit(virt, 0x100, idx, val); }
};

class GpioInReg : public GpioBitFieldReg {
public:
    static auto GetVal(void* virt, int idx) { return GetBit(virt, 0x200, idx); }
};

} // namespace gpio

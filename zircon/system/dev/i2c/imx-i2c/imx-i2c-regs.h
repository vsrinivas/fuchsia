// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace imx_i2c {

class SlaveAddressReg : public hwreg::RegisterBase<SlaveAddressReg, uint16_t> {
public:
    DEF_FIELD(7, 1, address);
    static auto Get() { return hwreg::RegisterAddr<SlaveAddressReg>(0x0); }
};

class FreqReg : public hwreg::RegisterBase<FreqReg, uint16_t> {
public:
    DEF_FIELD(5, 0, freq);
    static auto Get() { return hwreg::RegisterAddr<FreqReg>(0x4); }
};

class ControlReg : public hwreg::RegisterBase<ControlReg, uint16_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(7, enable);
    DEF_BIT(6, interrupt_enable);
    DEF_BIT(5, master);
    DEF_BIT(4, transmit);
    DEF_BIT(3, tx_ack_disable);
    DEF_BIT(2, repeat_start);
    static auto Get() { return hwreg::RegisterAddr<ControlReg>(0x8); }
};

class StatusReg : public hwreg::RegisterBase<StatusReg, uint16_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(7, transfer_complete);
    DEF_BIT(6, addressed_as_slave);
    DEF_BIT(5, bus_busy);
    DEF_BIT(4, arbitration_lost);
    DEF_BIT(2, slave_read_write);
    DEF_BIT(1, interrupt_pending);
    DEF_BIT(0, ack);
    static auto Get() { return hwreg::RegisterAddr<StatusReg>(0xC); }
};

// The register is 16 bits but since we only use 8 bits define as uint8_t to avoid casting
class DataReg : public hwreg::RegisterBase<DataReg, uint8_t> {
public:
    DEF_FIELD(7, 0, data);
    static auto Get() { return hwreg::RegisterAddr<DataReg>(0x10); }
};

} // namespace imx_i2c

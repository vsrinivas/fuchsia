// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace serial {

constexpr uint32_t AML_UART_WFIFO = 0x0;
constexpr uint32_t AML_UART_RFIFO = 0x4;

class Control : public hwreg::RegisterBase<Control, uint32_t, hwreg::EnablePrinter> {
public:
    enum XmitLength {
        kXmitLength8 = 0,
        kXmitLength7 = 1,
        kXmitLength6 = 2,
        kXmitLength5 = 3,
    };

    enum Parity {
        kParityNone = 0,
        kParityEven = 2,
        kParityOdd = 3,
    };

    enum StopLength {
        kStopLen1 = 0,
        kStopLen2 = 1,
    };

    DEF_BIT(31, inv_rts);
    DEF_BIT(30, mask_err);
    DEF_BIT(29, inv_cts);
    DEF_BIT(28, tx_interrupt_enable);
    DEF_BIT(27, rx_interrupt_enable);
    DEF_BIT(26, inv_tx);
    DEF_BIT(25, clear_error);
    DEF_BIT(24, rst_rx);
    DEF_BIT(22, rst_tx);
    DEF_FIELD(21, 20, xmit_len);
    DEF_FIELD(19, 18, parity);
    DEF_FIELD(17, 16, stop_len);
    DEF_BIT(15, two_wire);
    DEF_BIT(13, rx_enable);
    DEF_BIT(12, tx_enable);
    DEF_FIELD(11, 0, baud0);

    static auto Get() { return hwreg::RegisterAddr<Control>(0x8); }
};

class Status : public hwreg::RegisterBase<Status, uint32_t> {
public:
    DEF_BIT(26, rx_busy);
    DEF_BIT(25, tx_busy);
    DEF_BIT(24, rx_overflow);
    DEF_BIT(23, cts_level);
    DEF_BIT(22, tx_empty);
    DEF_BIT(21, tx_full);
    DEF_BIT(20, rx_empty);
    DEF_BIT(19, rx_full);
    DEF_BIT(18, tx_overflow);
    DEF_BIT(17, frame_error);
    DEF_BIT(16, parity_error);
    DEF_FIELD(15, 8, tx_count);
    DEF_FIELD(7, 0, rx_count);

    static auto Get() { return hwreg::RegisterAddr<Status>(0xC); }
};

class Misc : public hwreg::RegisterBase<Misc, uint32_t> {
public:
    DEF_FIELD(15, 8, xmit_irq_count);
    DEF_FIELD(7, 0, recv_irq_count);

    static auto Get() { return hwreg::RegisterAddr<Misc>(0x10); }
};

class Reg5 : public hwreg::RegisterBase<Reg5, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(26, xtal_tick);
    DEF_BIT(24, use_xtal_clk);
    DEF_BIT(23, use_new_baud_rate);
    DEF_FIELD(22, 0, new_baud_rate);

    static auto Get() { return hwreg::RegisterAddr<Reg5>(0x14); }
};

#define AML_UART_REG5_NEW_BAUD_RATE_MASK 0x7fffff

} // namespace serial

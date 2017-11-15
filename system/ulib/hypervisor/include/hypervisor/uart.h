// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/io.h>

static const size_t kUartBufferSize = 128;

class Guest;

// Stores the state of a UART.
class Uart : public IoHandler {
public:
    zx_status_t Init(Guest* guest, uint64_t addr);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* io) override;
    zx_status_t Write(uint64_t addr, const IoValue& io) override;

private:
    // Transmit holding register (THR).
    uint8_t tx_buffer_[kUartBufferSize] = {};
    uint16_t tx_offset_ = 0;

    // Interrupt enable register (IER).
    uint8_t interrupt_enable_ = 0;
    // Line control register (LCR).
    uint8_t line_control_ = 0;
};

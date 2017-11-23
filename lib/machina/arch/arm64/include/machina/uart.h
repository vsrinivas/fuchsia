// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/io.h>

class Guest;

// Implements the PL011 UART.
class Pl011 : public IoHandler {
public:
    zx_status_t Init(Guest* guest, uint64_t addr);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* io) const override;
    zx_status_t Write(uint64_t addr, const IoValue& io) override;

private:
    static const size_t kBufferSize = 128;

    uint8_t tx_buffer_[kBufferSize] = {};
    uint16_t tx_offset_ = 0;

    void Print(uint8_t ch);
};

using Uart = Pl011;

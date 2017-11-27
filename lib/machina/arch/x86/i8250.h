// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_I8250_H_
#define GARNET_LIB_MACHINA_ARCH_X86_I8250_H_

#include <fbl/mutex.h>
#include <hypervisor/io.h>

class Guest;

// Implements the I8250 UART.
class I8250 : public IoHandler {
public:
    zx_status_t Init(Guest* guest, uint64_t addr);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* io) const override;
    zx_status_t Write(uint64_t addr, const IoValue& io) override;

private:
    static const size_t kBufferSize = 128;
    mutable fbl::Mutex mutex_;

    uint8_t tx_buffer_[kBufferSize] = {};
    uint16_t tx_offset_ = 0;

    uint8_t interrupt_enable_ __TA_GUARDED(mutex_) = 0;
    uint8_t line_control_ __TA_GUARDED(mutex_) = 0;

    void Print(uint8_t ch);
};

#endif  // GARNET_LIB_MACHINA_ARCH_X86_I8250_H_

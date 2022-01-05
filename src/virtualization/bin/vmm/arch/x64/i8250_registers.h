// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_REGISTERS_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_REGISTERS_H_

#include <cstdint>

// I8250 state flags.
constexpr uint64_t kI8250LineStatusEmpty = 1u << 5;
constexpr uint64_t kI8250LineStatusIdle = 1u << 6;

constexpr uint64_t kI8250InterruptEnableTransmitEmpty = 1u << 1;

constexpr uint64_t kI8250InterruptIdTransmitEmpty = 1u << 1;
constexpr uint64_t kI8250InterruptIdNoInterrupt = 1;

constexpr uint64_t kI8250Base0 = 0x3f8;
constexpr uint64_t kI8250Base1 = 0x2f8;
constexpr uint64_t kI8250Base2 = 0x3e8;
constexpr uint64_t kI8250Base3 = 0x2e8;
constexpr uint64_t kI8250Size = 0x8;

constexpr uint64_t kI8250Irq0 = 4;
constexpr uint64_t kI8250Irq1 = 3;
constexpr uint64_t kI8250Irq2 = 4;
constexpr uint64_t kI8250Irq3 = 3;

// clang-format off

// I8250 registers.
enum I8250Register : uint8_t {
    RECEIVE             = 0x0,
    TRANSMIT            = 0x0,
    INTERRUPT_ENABLE    = 0x1,
    INTERRUPT_ID        = 0x2,
    LINE_CONTROL        = 0x3,
    MODEM_CONTROL       = 0x4,
    LINE_STATUS         = 0x5,
    MODEM_STATUS        = 0x6,
    SCRATCH             = 0x7,
};

// clang-format on

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_REGISTERS_H_

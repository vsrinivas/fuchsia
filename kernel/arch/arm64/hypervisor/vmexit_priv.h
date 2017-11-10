// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/atomic.h>
#include <zircon/types.h>

// clang-format off

// Exception class of an exception syndrome.
enum class ExceptionClass : uint8_t {
    SYSTEM_INSTRUCTION  = 0b011000,
    INSTRUCTION_ABORT   = 0b100000,
    DATA_ABORT          = 0b100100,
};


// Exception syndrome for a VM exit.
struct ExceptionSyndrome {
    ExceptionClass ec;
    uint32_t iss;

    ExceptionSyndrome(uint32_t esr);
};

enum class SystemRegister : uint16_t {
    SCTLR_EL1           = 0b11000000 << 8 /* op */ | 0b00010000 /* cr */,
};

// System instruction that caused a VM exit.
struct SystemInstruction {
    SystemRegister sr;
    uint8_t xt;
    bool read;

    SystemInstruction(uint32_t iss);
};

// clang-format on

typedef struct zx_port_packet zx_port_packet_t;

class GuestPhysicalAddressSpace;
struct GuestState;
class TrapMap;

zx_status_t vmexit_handler(GuestState* guest_state, fbl::atomic<uint64_t>* hcr,
                           GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                           zx_port_packet_t* packet);

// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

// clang-format off

// Exception class of the exception syndrome.
enum class ExceptionClass : uint8_t {
    INSTRUCTION_ABORT   = 0b100000,
    DATA_ABORT          = 0b100100,
};

// clang-format on

// Exception syndrome for the VM exit.
struct ExceptionSyndrome {
    ExceptionClass ec;
    uint32_t iss;

    ExceptionSyndrome(uint32_t esr);
};

// Data abort for the VM exit.
struct DataAbort {
    bool valid;
    bool write;

    DataAbort(uint32_t iss);
};

typedef struct zx_port_packet zx_port_packet_t;

class GuestPhysicalAddressSpace;
struct GuestState;
class TrapMap;

zx_status_t vmexit_handler(GuestState* guest_state, GuestPhysicalAddressSpace* gpas, TrapMap* traps,
                           zx_port_packet_t* packet);

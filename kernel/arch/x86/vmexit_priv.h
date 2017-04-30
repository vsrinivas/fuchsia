// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

class FifoDispatcher;
class GuestPhysicalAddressSpace;
struct GuestState;
struct IoApicState;
struct LocalApicState;
struct VmxState;

struct Instruction {
    bool read;
    bool rex;
    uint64_t val;
    uint64_t* reg;
};

status_t decode_instruction(const uint8_t* inst_buf, uint32_t inst_len, GuestState* guest_state,
                            Instruction* inst);

status_t vmexit_handler(GuestState* guest_state, LocalApicState* local_apic_state,
                        IoApicState* io_apic_state, GuestPhysicalAddressSpace* gpas,
                        FifoDispatcher* serial_fifo);

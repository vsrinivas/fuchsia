/*
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "third_party/processor-trace/libipt/include/intel-pt.h"

namespace simple_pt {

struct Instruction {
  // The total instruction count thus far.
  uint64_t tic;

  uint64_t cr3;
  uint64_t pc;

  // The destination of the branch/call.
  uint64_t dst;

  // The timestamp of this instruction.
  // The units depends on how the trace was made.
  // If the value is zero, either the value is unknown or it is the same
  // value as the previous instruction - the frequency at which timing packets
  // are emitted is configurable so there will be gaps.
  uint64_t ts;

  // See intel-pt.h:pt_insn_time.
  uint32_t lost_mtc;
  uint32_t lost_cyc;

  enum pt_insn_class iclass;

  // The number of instructions since the last record was emitted.
  unsigned insn_delta;

  // The core bus ratio. See intel docs on the CBR packet and
  // intel-pt.h:pt_insn_core_bus_ratio.
  // If the value is zero, either the value is unknown or it is the same
  // value as the previous instruction.
  uint32_t core_bus_ratio;

  // See intel-pt.h.
  unsigned speculative : 1, aborted : 1, committed : 1, disabled : 1,
      enabled : 1, resumed : 1, interrupted : 1, resynced : 1, stopped : 1;
};

void TransferEvents(Instruction* insn, const struct pt_insn* raw_insn);

}  // namespace simple_pt

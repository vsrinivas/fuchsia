// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_DAP_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_DAP_H_

#include <stdio.h>

#include <kernel/mp.h>

struct arm64_dap_processor_state {
  uint64_t r[31];
  uint64_t sp;
  uint64_t pc;
  uint64_t cpsr;
  uint32_t edscr;

  uint64_t esr_el1;
  uint64_t far_el1;
  uint64_t elr_el1;

  uint64_t esr_el2;
  uint64_t far_el2;
  uint64_t elr_el2;

  // bits [9:8] of EDSCR is the EL level of the cpu
  uint8_t get_el_level() const { return (edscr >> 8) & 0x3; }

  void Dump(FILE *fp = stdout);
};

bool arm64_dap_is_enabled();

// Attempt to use the DAP debugger interface to put the victim cpu into
// the debug state and get a snapshot of its register state.
//
// NOTE: will leave the cpu in a stuck state. Also makes no attempt to validate
// that the current cpu is the victim cpu. Suggest pinning the code to a single
// cpu that is something other than the victim.
zx_status_t arm64_dap_read_processor_state(cpu_num_t victim, arm64_dap_processor_state *state);

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_DAP_H_

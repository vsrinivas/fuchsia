// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Microarchitecture-specific workarounds and optimizations

#include <arch/arm64/mp.h>
#include <dev/psci.h>

bool arm64_uarch_needs_spectre_v2_mitigation() {
  switch (arm64_read_percpu_ptr()->microarch) {
  case ARM_CORTEX_A57:
  case ARM_CORTEX_A72:
  case ARM_CORTEX_A73:
  case ARM_CORTEX_A75:
  case CAVIUM_CN99XX:
    return true;
  default:
    return false;
  }
}

void arm64_uarch_do_spectre_v2_mitigation() {
  // Certain processors are vulnerable to branch target injection attacks (Spectre V2), where the
  // targets of indirect branches may be controlled by hostile code under speculation. The wrong
  // path speculation may leak secrets via cache side channels.
  //
  // Invalidate indirect branch predictors to guard the kernel by executing a PSCI call
  // TODO(fxbug.dev/33667): Use SMCCC ARCH_WORKAROUND and v1.1 calling convention if available.
  psci_get_version();
}

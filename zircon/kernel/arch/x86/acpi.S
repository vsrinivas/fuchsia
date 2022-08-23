// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//

#include <asm.h>

.code64
.section .text
// zx_status_t x86_acpi_transition_s_state(struct x86_realmode_entry_data_registers* reg,
//                                         uint8_t target_s_state, uint8_t sleep_type_a,
//                                         uint8_t sleep_type_b)
FUNCTION_LABEL(x86_acpi_transition_s_state)
    // For now just return ZX_ERR_NOT_SUPPORTED (-2)
    movq %rax, -2
    ret

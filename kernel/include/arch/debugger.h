// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>
#include <stdbool.h>

__BEGIN_CDECLS

struct thread;

// Regset #0 is defined to be the general regs.
// The remaining regsets are architecture-specific.
// By convention the "general regs" are, generally, the integer regs + pc
// + ALU flags.

uint arch_num_regsets(void);

status_t arch_get_regset(struct thread *thread, uint regset, void* regs, uint* buf_size);
status_t arch_set_regset(struct thread *thread, uint regset, const void* regs, uint buf_size);

__END_CDECLS

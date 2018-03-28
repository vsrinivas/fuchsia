// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

__BEGIN_CDECLS

struct thread;

// The caller is responsible for making sure the thread is in an exception
// or is suspended, and stays so.
zx_status_t arch_get_general_regs(struct thread* thread, zx_thread_state_general_regs* out);
zx_status_t arch_set_general_regs(struct thread* thread, const zx_thread_state_general_regs* in);

zx_status_t arch_get_single_step(struct thread* thread, bool* single_step);
zx_status_t arch_set_single_step(struct thread* thread, bool single_step);

__END_CDECLS

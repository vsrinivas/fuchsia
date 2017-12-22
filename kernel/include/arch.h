// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

void arch_early_init(void);
void arch_init(void);

// Called just before initiating a system suspend to give the arch layer a
// chance to save state.  Must be called with interrupts disabled.
void arch_suspend(void);
// Called immediately after resuming from a system suspend to let the arch layer
// reinitialize arch components.  Must be called with interrupts disabled.
void arch_resume(void);

void arch_enter_uspace(uintptr_t entry_point, uintptr_t sp,
                       uintptr_t arg1, uintptr_t arg2) __NO_RETURN;

__END_CDECLS

/* arch specific bits */
#include <arch/defines.h>

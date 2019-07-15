// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_H_

#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

struct iframe_t;

void arch_early_init(void);
void arch_init(void);

// Called just before initiating a system suspend to give the arch layer a
// chance to save state.  Must be called with interrupts disabled.
void arch_suspend(void);
// Called immediately after resuming from a system suspend to let the arch layer
// reinitialize arch components.  Must be called with interrupts disabled.
void arch_resume(void);

// Initialize |iframe| for running a userspace thread.
// The rest of the current thread's state must already have been
// appropriately initialized (as viewable from a debugger at the
// ZX_EXCP_THREAD_STARTING exception).
void arch_setup_uspace_iframe(iframe_t* iframe, uintptr_t entry_point, uintptr_t sp, uintptr_t arg1,
                              uintptr_t arg2);

// Enter userspace.
// |iframe| is generally initialized with |arch_setup_uspace_iframe()|.
void arch_enter_uspace(iframe_t* iframe) __NO_RETURN;

__END_CDECLS

/* arch specific bits */
#include <arch/defines.h>

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_H_

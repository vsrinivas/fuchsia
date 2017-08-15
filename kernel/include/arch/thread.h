// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// give the arch code a chance to declare the arch_thread struct
#include <arch/arch_thread.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

struct thread;

void arch_thread_initialize(struct thread *, vaddr_t entry_point);
void arch_context_switch(struct thread *oldthread, struct thread *newthread);
void arch_thread_construct_first(struct thread *);

__END_CDECLS

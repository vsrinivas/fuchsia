// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <arch/x86/registers.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct arch_thread {
    vaddr_t sp;
#if ARCH_X86_64
    vaddr_t fs_base;
    vaddr_t gs_base;
#endif

    /* buffer to save fpu state */
    vaddr_t *extended_register_state;
    uint8_t extended_register_buffer[X86_MAX_EXTENDED_REGISTER_SIZE + 64];

    /* if non-NULL, address to return to on page fault */
    void *page_fault_resume;
};

__END_CDECLS

// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct fpstate {
    uint32_t    fpcr;
    uint32_t    fpsr;
    uint64_t    regs[64];
};

struct arch_thread {
    vaddr_t sp;

    /* if non-NULL, address to return to on data fault */
    void *data_fault_resume;

    /* saved fpu state */
    struct fpstate fpstate;
};

__END_CDECLS

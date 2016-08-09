// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct arch_thread {
    vaddr_t sp;

    /* if non-NULL, address to return to on data fault */
    void *data_fault_resume;

#if ARM_WITH_VFP
    /* has this thread ever used the floating point state? */
    bool fpused;

    uint32_t fpscr;
    uint32_t fpexc;
    double   fpregs[32];
#endif
};

__END_CDECLS

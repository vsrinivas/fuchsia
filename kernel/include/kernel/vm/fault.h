// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

// page fault flags
const uint VMM_PF_FLAG_WRITE = (1u << 0);
const uint VMM_PF_FLAG_USER = (1u << 1);
const uint VMM_PF_FLAG_INSTRUCTION = (1u << 2);
const uint VMM_PF_FLAG_NOT_PRESENT = (1u << 3);
const uint VMM_PF_FLAG_HW_FAULT = (1u << 4); // hardware is requesting a fault
const uint VMM_PF_FLAG_SW_FAULT = (1u << 5); // software fault
const uint VMM_PF_FLAG_FAULT_MASK = (VMM_PF_FLAG_HW_FAULT | VMM_PF_FLAG_SW_FAULT);

// convenience routine for convering page fault flags to a string
static const char* vmm_pf_flags_to_string(uint pf_flags, char str[5]) {
    str[0] = (pf_flags & VMM_PF_FLAG_WRITE) ? 'w' : 'r';
    str[1] = (pf_flags & VMM_PF_FLAG_USER) ? 'u' : 's';
    str[2] = (pf_flags & VMM_PF_FLAG_INSTRUCTION) ? 'i' : 'd';
    str[3] = (pf_flags & VMM_PF_FLAG_NOT_PRESENT) ? 'n' : 'p';
    str[4] = '\0';

    return str;
}

// page fault handler, called during page fault context, with interrupts enabled
status_t vmm_page_fault_handler(vaddr_t addr, uint pf_flags);

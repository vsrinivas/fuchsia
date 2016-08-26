// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <list.h>

__BEGIN_CDECLS

#define ARCH_ASPACE_MAGIC 0x41524153 // ARAS

struct arch_aspace {
    /* magic value for use-after-free detection */
    uint32_t magic;

    /* pointer to the translation table */
    paddr_t tt_phys;
    uint32_t *tt_virt;

    /* range of address space */
    vaddr_t base;
    size_t size;

    /* list of pages allocated for these page tables */
    struct list_node pt_page_list;
};

__END_CDECLS

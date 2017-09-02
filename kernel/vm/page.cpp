// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <stdio.h>
#include <string.h>
#include <vm/page.h>
#include <vm/pmm.h>

const char* page_state_to_string(unsigned int state) {
    switch (state) {
    case VM_PAGE_STATE_FREE:
        return "free";
    case VM_PAGE_STATE_ALLOC:
        return "alloc";
    case VM_PAGE_STATE_WIRED:
        return "wired";
    case VM_PAGE_STATE_HEAP:
        return "heap";
    case VM_PAGE_STATE_OBJECT:
        return "object";
    case VM_PAGE_STATE_MMU:
        return "mmu";
    default:
        return "unknown";
    }
}

void dump_page(const vm_page_t* page) {
    printf("page %p: address %#" PRIxPTR " state %s flags %#x\n", page, vm_page_to_paddr(page),
           page_state_to_string(page->state), page->flags);
}

static int cmd_vm_page(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump <address>\n", argv[0].str);
        printf("%s hexdump <address>\n", argv[0].str);
        return MX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        if (argc < 2)
            goto notenoughargs;

        vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

        dump_page(page);
    } else if (!strcmp(argv[1].str, "hexdump")) {
        if (argc < 2)
            goto notenoughargs;

        vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

        paddr_t pa = vm_page_to_paddr(page);
        void* ptr = paddr_to_kvaddr(pa);
        if (!ptr) {
            printf("bad page or page not mapped in kernel space\n");
            return -1;
        }
        hexdump(ptr, PAGE_SIZE);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return MX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vm_page", "vm_page debug commands", &cmd_vm_page)
#endif
STATIC_COMMAND_END(vm_page);

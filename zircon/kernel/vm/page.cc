// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/page.h"

#include <inttypes.h>
#include <lib/console.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>

#include <kernel/percpu.h>
#include <pretty/hexdump.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

void vm_page::dump() const {
  const vm_page_state page_state = state();
  printf("page %p: address %#" PRIxPTR " state %s", this, paddr(),
         page_state_to_string(page_state));
  if (page_state == vm_page_state::OBJECT) {
    printf(" pin_count %d split_bits %d%d\n", object.get_pin_count(), object.cow_left_split,
           object.cow_right_split);
  } else {
    printf("\n");
  }
}

void vm_page::set_state(vm_page_state new_state) {
  const vm_page_state old_state = state();
  state_priv.store(new_state, ktl::memory_order_relaxed);

  // By only modifying the counters for the current CPU with preemption disabled, we can ensure
  // the values are not modified concurrently. See comment at the definition of |vm_page_counts|.
  percpu::WithCurrentPreemptDisable([&old_state, &new_state](percpu* p) {
    // Be sure to not block, else we lose the protection provided by disabling preemption.
    p->vm_page_counts.by_state[VmPageStateIndex(old_state)] -= 1;
    p->vm_page_counts.by_state[VmPageStateIndex(new_state)] += 1;
  });

  if (unlikely(object.page_offset_priv == kObjectOrEventIsEvent && old_state == vm_page_state::OBJECT && new_state == vm_page_state::FREE)) {
    object.signal_waiters();
  }
}

uint64_t vm_page::get_count(vm_page_state state) {
  int64_t result = 0;
  percpu::ForEachPreemptDisable([&state, &result](percpu* p) {
    // Because |get_count| could be called concurrently with |set_state| we're not guaranteed to
    // get a consistent snapshot of the page counts. It's OK if the values are a little off. See
    // comment at the definition of |vm_page_state|.
    result += p->vm_page_counts.by_state[VmPageStateIndex(state)];
  });
  return result >= 0 ? result : 0;
}

void vm_page::add_to_initial_count(vm_page_state state, uint64_t n) {
  percpu::WithCurrentPreemptDisable(
      [&state, &n](percpu* p) { p->vm_page_counts.by_state[VmPageStateIndex(state)] += n; });
}

static int cmd_vm_page(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s dump <address>\n", argv[0].str);
    printf("%s hexdump <address>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

    page->dump();
  } else if (!strcmp(argv[1].str, "hexdump")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

    paddr_t pa = page->paddr();
    void* ptr = paddr_to_physmap(pa);
    if (!ptr) {
      printf("bad page or page not mapped in kernel space\n");
      return ZX_ERR_INTERNAL;
    }
    hexdump(ptr, PAGE_SIZE);
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("vm_page", "vm_page debug commands", &cmd_vm_page)
STATIC_COMMAND_END(vm_page)

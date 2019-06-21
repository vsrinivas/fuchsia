// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/*
 * Global init hook mechanism. Allows code anywhere in the system to define
 * a init hook that is called at increasing init levels as the system is
 * initialized.
 */
#include <arch/ops.h>
#include <lk/init.h>

#include <assert.h>
#include <debug.h>
#include <trace.h>
#include <zircon/compiler.h>

#define LOCAL_TRACE 0
#define TRACE_INIT (LK_DEBUGLEVEL >= 2)
#ifndef EARLIEST_TRACE_LEVEL
#define EARLIEST_TRACE_LEVEL LK_INIT_LEVEL_TARGET_EARLY
#endif

extern const struct lk_init_struct __start_lk_init[];
extern const struct lk_init_struct __stop_lk_init[];

void lk_init_level(enum lk_init_flags required_flag, uint start_level, uint stop_level) {
    LTRACEF("flags %#x, start_level %#x, stop_level %#x\n",
            (uint)required_flag, start_level, stop_level);

    ASSERT(start_level > 0);
    uint last_called_level = start_level - 1;
    const struct lk_init_struct* last = NULL;
    for (;;) {
        /* search for the lowest uncalled hook to call */
        LTRACEF("last %p, last_called_level %#x\n", last, last_called_level);

        const struct lk_init_struct* found = NULL;
        bool seen_last = false;
        for (const struct lk_init_struct* ptr = __start_lk_init; ptr != __stop_lk_init; ptr++) {
            LTRACEF("looking at %p (%s) level %#x, flags %#x, seen_last %d\n", ptr, ptr->name, ptr->level, ptr->flags, seen_last);

            if (ptr == last)
                seen_last = true;

            /* reject the easy ones */
            if (!(ptr->flags & required_flag))
                continue;
            if (ptr->level > stop_level)
                continue;
            if (ptr->level < last_called_level)
                continue;
            if (found && found->level <= ptr->level)
                continue;

            /* keep the lowest one we haven't called yet */
            if (ptr->level >= start_level && ptr->level > last_called_level) {
                found = ptr;
                continue;
            }

            /* if we're at the same level as the last one we called and we've
             * already passed over it this time around, we can mark this one
             * and early terminate the loop.
             */
            if (ptr->level == last_called_level && ptr != last && seen_last) {
                found = ptr;
                break;
            }
        }

        if (!found)
            break;

        if (TRACE_INIT) {
            if (found->level >= EARLIEST_TRACE_LEVEL) {
                printf("INIT: cpu %u, calling hook %p (%s) at level %#x, flags %#x\n",
                       arch_curr_cpu_num(), found->hook, found->name, found->level, found->flags);
            }
        }

        found->hook(found->level);
        last_called_level = found->level;
        last = found;
    }
}

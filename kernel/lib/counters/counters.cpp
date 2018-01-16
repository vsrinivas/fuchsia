// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>

#include <string.h>

#include <arch/ops.h>
#include <kernel/cmdline.h>
#include <kernel/percpu.h>

#include <lk/init.h>

#include <lib/console.h>

// The arena is allocated in kernel.ld, which see.
extern uint64_t kcounters_arena[];

static size_t get_num_counters() {
    return kcountdesc_end - kcountdesc_begin;
}

static bool prefix_match(const char *pre, const char *str) {
    return strncmp(pre, str, strlen(pre)) == 0;
}

// Binary search the sorted counter descriptors.
// We rely on SORT_BY_NAME() in the linker script for this to work.
static const k_counter_desc* upper_bound(
    const char* val, const k_counter_desc* first, const k_counter_desc* last) {
    if (first >= last)
        return last;

    const k_counter_desc* it;
    size_t step;
    auto count = last - first;

    while (count > 0) {
        step = count / 2;
        it = first + step;

        if (strcmp(it->name, val) < 0) {
            first = ++it;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

static void counters_init(unsigned level) {
    // Wire the memory defined in the .bss section to the counters.
    for (size_t ix = 0; ix != SMP_MAX_CPUS; ++ix) {
        percpu[ix].counters = &kcounters_arena[ix * get_num_counters()];
    }
}

static void dump_counter(const k_counter_desc* desc) {
    size_t counter_index = kcounter_index(desc);

    uint64_t sum = 0;
    uint64_t values[SMP_MAX_CPUS];
    for (size_t ix = 0; ix != SMP_MAX_CPUS; ++ix) {
        // This value is not atomically consistent, therefore is just
        // an approximation. TODO(cpu): for ARM this might need some magic.
        values[ix] = percpu[ix].counters[counter_index];
        sum += values[ix];
    }

    printf("[%.2zu] %s = %lu\n", counter_index, desc->name, sum);
    if (sum == 0u)
        return;

    // Print the per-core counts when the sum is not zero.
    printf("     ");
    for (size_t ix = 0; ix != SMP_MAX_CPUS; ++ix) {
        if (values[ix] > 0)
            printf("[%zu:%lu]", ix, values[ix]);
    }
    printf("\n");
}

static void dump_all_counters() {
    printf("%zu counters available:\n", get_num_counters());
    for (auto it = kcountdesc_begin; it != kcountdesc_end; ++it) {
        dump_counter(it);
    }
}

static int get_counter(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc == 2) {
        if (strcmp(argv[1].str, "--all") == 0) {
            dump_all_counters();
        } else {
            int num_results = 0;
            auto name = argv[1].str;
            auto desc = upper_bound(name, kcountdesc_begin, kcountdesc_end);
            while (desc != kcountdesc_end) {
                if (!prefix_match(name, desc->name))
                    break;
                dump_counter(desc);
                ++num_results;
                ++desc;
            }
            if (num_results == 0) {
                printf("counter '%s' not found, try --all\n", name);
            } else {
                printf("%d counters found\n", num_results);
            }
        }
    } else {
        printf("only '--all' or a counter name, or counter prefix allowed\n");
    }
    return 0;
}

LK_INIT_HOOK(kcounters, counters_init, LK_INIT_LEVEL_PLATFORM_EARLY);

STATIC_COMMAND_START
STATIC_COMMAND("counters", "get counter", &get_counter)
STATIC_COMMAND_END(mem_tests);

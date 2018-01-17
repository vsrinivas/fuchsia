// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>

#include <string.h>

#include <arch/ops.h>
#include <platform.h>

#include <kernel/auto_lock.h>
#include <kernel/cmdline.h>
#include <kernel/timer.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>

#include <fbl/alloc_checker.h>
#include <fbl/mutex.h>

#include <lk/init.h>

#include <lib/console.h>

// The arena is allocated in kernel.ld linker script.
extern uint64_t kcounters_arena[];

struct watched_counter_t {
    list_node node;
    const k_counter_desc* desc;
    // TODO(cpu): add min, max.
};

static fbl::Mutex watcher_lock;
static list_node watcher_list = LIST_INITIAL_VALUE(watcher_list);
static thread_t* watcher_thread;

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

static int watcher_thread_fn(void* arg) {
    while (true) {
        {
            fbl::AutoLock lock(&watcher_lock);
            if (list_is_empty(&watcher_list)) {
                watcher_thread = nullptr;
                return 0;
            }

            watched_counter_t* wc;
            list_for_every_entry (&watcher_list, wc, watched_counter_t, node) {
                dump_counter(wc->desc);
            }
        }

        thread_sleep_relative(ZX_SEC(2));
    }
}

static int view_counter(int argc, const cmd_args* argv) {
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
        printf(
            "counters view <counter-name>\n"
            "counters view <counter-prefix>\n"
            "counters view --all\n"
        );
        return 1;
    }

    return 0;
}

static int watch_counter(int argc, const cmd_args* argv) {
    if (argc == 2) {
        if (strcmp(argv[1].str, "--stop") == 0) {
            fbl::AutoLock lock(&watcher_lock);
            watched_counter_t* wc;
            while ((wc = list_remove_head_type(
                &watcher_list, watched_counter_t, node)) != nullptr) {
                delete wc;
            }
            // The thread exits itself it there are no counters.
            return 0;
        }

        size_t counter_id = argv[1].u;
        auto range = get_num_counters() - 1;
        if (counter_id > range) {
            printf("counter id must be in the 0 to %zu range\n", range);
            return 1;
        } else if ((counter_id == 0) && (strlen(argv[1].str) > 1)) {
            // Parsing a name as a number.
            printf("counter ids are numbers\n");
            return 1;
        }

        fbl::AllocChecker ac;
        auto wc = new (&ac) watched_counter_t {
            LIST_INITIAL_CLEARED_VALUE, &kcountdesc_begin[counter_id] };
        if (!ac.check()) {
            printf("no memory for counter\n");
            return 1;
        }

        {
            fbl::AutoLock lock(&watcher_lock);
            list_add_head(&watcher_list, &wc->node);
            if (watcher_thread == nullptr) {
                watcher_thread = thread_create(
                    "counter-watcher", watcher_thread_fn, nullptr, LOW_PRIORITY, 4096);
                if (watcher_thread == nullptr) {
                    printf("no memory for watcher thread\n");
                    return 1;
                }
                thread_detach_and_resume(watcher_thread);
            }
        }

    } else {
        printf(
            "counters watch <counter-id>\n"
            "counters watch --stop\n"
        );
    }

    return 0;
}

static int cmd_counters(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc > 1) {
        if (strcmp(argv[1].str, "view") == 0) {
            return view_counter(argc - 1, &argv[1]);
        }
        if (strcmp(argv[1].str, "watch") == 0) {
            return watch_counter(argc - 1, &argv[1]);
        }
    }

    printf(
        "inspect system counters:\n"
        "  counters view <name>\n"
        "  counters watch <id>\n"
    );
    return 0;
}

LK_INIT_HOOK(kcounters, counters_init, LK_INIT_LEVEL_PLATFORM_EARLY);

STATIC_COMMAND_START
STATIC_COMMAND("counters", "view system counters", &cmd_counters)
STATIC_COMMAND_END(mem_tests);

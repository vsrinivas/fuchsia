// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/oom.h>

#include <kernel/thread.h>
#include <vm/pmm.h>
#include <lib/console.h>
#include <magenta/errors.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <platform.h>
#include <pretty/sizes.h>

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

using fbl::AutoLock;

// Guards the oom_* values below.
static fbl::Mutex oom_mutex;

// Function to call when we hit a low-memory condition.
static oom_lowmem_callback_t* oom_lowmem_callback TA_GUARDED(oom_mutex);

// The thread, if it's running; nullptr otherwise.
static thread_t* oom_thread TA_GUARDED(oom_mutex);

// True if the thread should keep running.
static bool oom_running TA_GUARDED(oom_mutex);

// How long the OOM thread sleeps between checks.
static uint64_t oom_sleep_duration_ns TA_GUARDED(oom_mutex);

// If the PMM has fewer than this many bytes free, start killing processes.
static uint64_t oom_redline_bytes TA_GUARDED(oom_mutex);

// True if the thread should print the current free value when it runs.
static bool oom_printing TA_GUARDED(oom_mutex);

// True if the thread should simulate a low-memory condition on its next loop.
static bool oom_simulate_lowmem TA_GUARDED(oom_mutex);

static int oom_loop(void* arg) {
    const size_t total_bytes = pmm_count_total_bytes();
    char total_buf[MAX_FORMAT_SIZE_LEN];
    format_size_fixed(total_buf, sizeof(total_buf), total_bytes, 'M');

    size_t last_free_bytes = total_bytes;
    while (true) {
        const size_t free_bytes = pmm_count_free_pages() * PAGE_SIZE;

        bool lowmem = false;
        bool printing = false;
        size_t shortfall_bytes = 0;
        oom_lowmem_callback_t* lowmem_callback = nullptr;
        uint64_t sleep_duration_ns = 0;
        {
            AutoLock lock(&oom_mutex);
            if (!oom_running) {
                break;
            }
            if (oom_simulate_lowmem) {
                printf("OOM: simulating low-memory situation\n");
            }
            lowmem = free_bytes < oom_redline_bytes || oom_simulate_lowmem;
            if (lowmem) {
                shortfall_bytes =
                    oom_simulate_lowmem
                        ? 512 * 1024
                        : oom_redline_bytes - free_bytes;
            }
            oom_simulate_lowmem = false;

            printing =
                lowmem || (oom_printing && free_bytes != last_free_bytes);
            lowmem_callback = oom_lowmem_callback;
            DEBUG_ASSERT(lowmem_callback != nullptr);
            sleep_duration_ns = oom_sleep_duration_ns;
        }

        if (printing) {
            char free_buf[MAX_FORMAT_SIZE_LEN];
            format_size_fixed(free_buf, sizeof(free_buf), free_bytes, 'M');

            int64_t free_delta_bytes = free_bytes - last_free_bytes;
            char delta_sign = '+';
            if (free_delta_bytes < 0) {
                free_delta_bytes *= -1;
                delta_sign = '-';
            }
            char delta_buf[MAX_FORMAT_SIZE_LEN];
            format_size(delta_buf, sizeof(delta_buf), free_delta_bytes);

            printf("OOM: %s free (%c%s) / %s total\n",
                   free_buf,
                   delta_sign,
                   delta_buf,
                   total_buf);
        }
        last_free_bytes = free_bytes;

        if (lowmem) {
            lowmem_callback(shortfall_bytes);
        }

        thread_sleep_relative(sleep_duration_ns);
    }

    return 0;
}

static void start_thread_locked() TA_REQ(oom_mutex) {
    DEBUG_ASSERT(oom_thread == nullptr);
    DEBUG_ASSERT(oom_running == false);
    thread_t* t = thread_create("oom", oom_loop, nullptr,
                                HIGH_PRIORITY, DEFAULT_STACK_SIZE);
    if (t != nullptr) {
        oom_running = true;
        oom_thread = t;
        thread_resume(t);
        printf("OOM: started thread\n");
    } else {
        printf("OOM: failed to create thread\n");
    }
}

void oom_init(bool enable, uint64_t sleep_duration_ns, size_t redline_bytes,
              oom_lowmem_callback_t* lowmem_callback) {
    DEBUG_ASSERT(sleep_duration_ns > 0);
    DEBUG_ASSERT(redline_bytes > 0);
    DEBUG_ASSERT(lowmem_callback != nullptr);

    AutoLock lock(&oom_mutex);
    DEBUG_ASSERT(oom_lowmem_callback == nullptr);
    oom_lowmem_callback = lowmem_callback;
    oom_sleep_duration_ns = sleep_duration_ns;
    oom_redline_bytes = redline_bytes;
    oom_printing = false;
    oom_simulate_lowmem = false;
    if (enable) {
        start_thread_locked();
    } else {
        printf("OOM: thread disabled\n");
    }
}

static int cmd_oom(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
        printf("Not enough arguments:\n");
    usage:
        printf("oom start  : ensure that the OOM thread is running\n");
        printf("oom stop   : ensure that the OOM thread is not running\n");
        printf("oom info   : dump OOM params/state\n");
        printf("oom print  : continually print free memory (toggle)\n");
        printf("oom lowmem : act as if the redline was just hit (once)\n");
        return -1;
    }

    AutoLock lock(&oom_mutex);
    if (strcmp(argv[1].str, "start") == 0) {
        if (!oom_running) {
            start_thread_locked();
        } else {
            printf("OOM thread already running\n");
        }
    } else if (strcmp(argv[1].str, "stop") == 0) {
        if (oom_running) {
            printf("Stopping OOM thread...\n");
            oom_running = false;
            thread_t* t = oom_thread;
            oom_thread = nullptr;
            lk_time_t deadline = current_time() + 4 * oom_sleep_duration_ns;

            lock.release();
            mx_status_t s = thread_join(t, nullptr, deadline);
            if (s == MX_OK) {
                printf("OOM thread stopped.\n");
            } else {
                printf("Error stopping OOM thread: %d\n", s);
            }
            // We released the mutex; avoid executing any further.
            return 0;
        } else {
            printf("OOM thread already stopped\n");
        }
    } else if (strcmp(argv[1].str, "info") == 0) {
        printf("OOM info:\n");
        printf("  running: %s\n", oom_running ? "true" : "false");
        printf("  printing: %s\n", oom_printing ? "true" : "false");
        printf("  simulating lowmem: %s\n",
               oom_simulate_lowmem ? "true" : "false");

        printf("  sleep duration: %" PRIu64 "ms\n",
               oom_sleep_duration_ns / 1000000);

        char buf[MAX_FORMAT_SIZE_LEN];
        format_size_fixed(buf, sizeof(buf), oom_redline_bytes, 'M');
        printf("  redline: %s (%" PRIu64 " bytes)\n", buf, oom_redline_bytes);
    } else if (strcmp(argv[1].str, "print") == 0) {
        oom_printing = !oom_printing;
        printf("OOM print is now %s\n", oom_printing ? "on" : "off");
    } else if (strcmp(argv[1].str, "lowmem") == 0) {
        oom_simulate_lowmem = true;
    } else {
        printf("Unrecognized subcommand '%s'\n", argv[1].str);
        goto usage;
    }
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("oom", "out-of-memory watcher/killer", &cmd_oom)
STATIC_COMMAND_END(oom);

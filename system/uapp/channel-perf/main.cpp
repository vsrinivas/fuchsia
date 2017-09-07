// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

namespace {

void argument_error(const char* argv0, const char* message) {
    fprintf(stderr, "%s: error: %s\nRun with -h for help.\n", argv0, message);
    exit(EXIT_FAILURE);
}

void duplicate_handles(uint32_t n, mx_handle_t src, mx_handle_t* dest) {
    for (uint32_t i = 0; i < n; i++) {
        assert(mx_handle_duplicate(src, MX_RIGHT_SAME_RIGHTS, &dest[i]) == 0);
    }
}

struct TestArgs {
    uint32_t size;
    uint32_t handles;
    uint32_t queue;
};

void do_test(uint32_t duration, const TestArgs& test_args) {
    __UNUSED mx_status_t status;

    uint64_t duration_ns = duration * 1000000000ull;

    // We'll write to mp[0] (and read from mp[1]).
    mx_handle_t mp[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
    status = mx_channel_create(0u, &mp[0], &mp[1]);
    assert(status == MX_OK);

    // We'll send/receive duplicates of this handle.
    mx_handle_t event;
    assert(mx_event_create(0u, &event) == MX_OK);

    // Storage space for our messages' stuff.
    fbl::unique_ptr<uint8_t[]> data;
    if (test_args.size) {
        data.reset(new uint8_t[test_args.size]);
        for (uint32_t i = 0; i < test_args.size; i++)
            data[i] = static_cast<uint8_t>(i);
    }
    fbl::unique_ptr<mx_handle_t[]> handles;
    if (test_args.handles)
        handles.reset(new mx_handle_t[test_args.handles]);

    // Pre-queue |test_args.queue| messages (there'll always be this many messages in the queue).
    for (uint32_t i = 0; i < test_args.queue; i++) {
        duplicate_handles(test_args.handles, event, handles.get());
        status = mx_channel_write(mp[0], 0u, data.get(), test_args.size,
                                  handles.get(), test_args.handles);
        assert(status == MX_OK);
    }

    duplicate_handles(test_args.handles, event, handles.get());

    static constexpr uint32_t big_it_size = 10000;
    uint64_t big_its = 0;
    uint64_t start_ns = mx_time_get(MX_CLOCK_MONOTONIC);
    uint64_t end_ns;
    for (;;) {
        big_its++;
        for (uint32_t i = 0; i < big_it_size; i++) {
            status = mx_channel_write(mp[0], 0, data.get(), test_args.size,
                                      handles.get(), test_args.handles);
            assert(status == MX_OK);

            uint32_t r_size = test_args.size;
            uint32_t r_handles = test_args.handles;
            status = mx_channel_read(mp[1], 0u, data.get(), handles.get(), r_size,
                                     r_handles, &r_size, &r_handles);
            assert(status == MX_OK);
            assert(r_size == test_args.size);
            assert(r_handles == test_args.handles);
        }

        end_ns = mx_time_get(MX_CLOCK_MONOTONIC);
        if ((end_ns - start_ns) >= duration_ns)
            break;
    }

    for (uint32_t i = 0; i < test_args.handles; i++) {
        status = mx_handle_close(handles[i]);
        assert(status == MX_OK);
    }
    status = mx_handle_close(event);
    assert(status == MX_OK);
    status = mx_handle_close(mp[0]);
    assert(status == MX_OK);
    status = mx_handle_close(mp[1]);
    assert(status == MX_OK);

    double real_duration = static_cast<double>(end_ns - start_ns) / 1000000000.0;
    double its_per_second = static_cast<double>(big_its) * big_it_size / real_duration;
    printf("write/read %" PRIu32 " bytes, %" PRIu32 " handles (%" PRIu32 " pre-queued): "
               "%.0f iterations/second\n",
           test_args.size, test_args.handles, test_args.queue, its_per_second);
}

}  // namespace

int main(int argc, char** argv) {
    static constexpr char help[] =
        "Usage: %s [options ...]\n"
        "\n"
        "Options:\n"
        "  -h    show help (this)\n"
        "  -o    run single test (default)\n"
        "  -s    run suite (ignores -S/-H/-Q)\n"
        "  -n N  set test repetition count to N (default: 1)\n"
        "  -d N  set test duration to N seconds (default: 5)\n"
        "  -S N  set message size to N bytes (default: 10)\n"
        "  -H N  set message handle count to N handles (default: 0)\n"
        "  -Q N  set message pre-queue count to N messages (default: 0)\n";

    bool run_suite = false;  // -o/-s
    uint32_t duration = 5;   // -d
    uint32_t repeats = 1;    // -n
    // Ignored when running a suite:
    TestArgs test_args = {
        10,                  // -S (size)
        0,                   // -H (handles)
        0                    // -Q (queue)
    };

    int opt;
    while ((opt = getopt(argc, argv, "+hosn:d:S:H:Q:")) != -1) {
        // Our option values are always unsigned numbers.
        uint32_t value = 0;
        if (optarg) {
            errno = 0;
            char* endptr = nullptr;
            unsigned long long v = strtoull(optarg, &endptr, 10);
            if (errno != 0 || *endptr != '\0' || value > UINT32_MAX)
                argument_error(argv[0], "invalid numeric optional value");
            value = static_cast<uint32_t>(v);
        }

        switch (opt) {
            case 'h':
                printf(help, argv[0]);
                return EXIT_SUCCESS;
            case 'o':
                run_suite = false;
                break;
            case 's':
                run_suite = true;
                break;
            case 'n':
                assert(optarg);
                repeats = value;
                break;
            case 'd':
                assert(optarg);
                duration = value;
                break;
            case 'S':
                assert(optarg);
                test_args.size = value;
                break;
            case 'H':
                assert(optarg);
                test_args.handles = value;
                break;
            case 'Q':
                assert(optarg);
                test_args.queue = value;
                break;
            default:  // '?'
                argument_error(argv[0], "invalid option");
                break;
        }
    }
    if (optind < argc)
        argument_error(argv[0], "unexpected positional argument");

    for (uint32_t i = 0; i < repeats; i++) {
        if (repeats > 1u) {
            if (i > 0u)
                printf("\n");
            printf("Test iteration #%" PRIu32 " (of %" PRIu32 "):\n", i + 1,
                   repeats);
        }

        if (run_suite) {
            static constexpr TestArgs suite[] = {
                {10, 0, 0},
                {100, 0, 0},
                {1000, 0, 0},
                {10, 1, 0},
                {100, 1, 0},
                {1000, 1, 0},
                {10, 2, 0},
                {100, 2, 0},
                {1000, 2, 0},
                {10, 5, 0},
                {100, 5, 0},
                {1000, 5, 0},
                {10, 0, 1},
                {100, 0, 1},
                {1000, 0, 1},
            };
            for (size_t i = 0; i < fbl::count_of(suite); i++)
                do_test(duration, suite[i]);
        } else {
            do_test(duration, test_args);
        }
    }

    return EXIT_SUCCESS;
}

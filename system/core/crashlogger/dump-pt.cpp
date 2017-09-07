// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>

#include "utils.h"

#if defined(__x86_64__)  // entire file

constexpr char ipt_program[] = "/system/bin/ipt";

// Format string for dump output file prefix.
// PT dumps consist of several files, all beginning with this prefix.
constexpr char pt_path_prefix[] = "/tmp/crash-pt";

// The format of the path prefix, without the file suffix.
// The full name of dump files is $pt_path_prefix.num$seq.$suffix.
#define PT_PATH_FORMAT "%s.num%d"

// Test file suffix. This is the where PT buffer data is written.
// If $pt_path_prefix.$seq.$suffix doesn't exist then we use $seq.
constexpr char pt_file_test_suffix[] = "pt";

// Every dump is written to a new set of files:
// This counts to kMaxIptDumps and resets.
// When the max number of files has been written we don't write any more
// until at least one set of files has been deleted.
static int next_seq_num = 0;
constexpr int kMaxIptDumps = 4;

// Don't wait forever for ipt to run.
// It may take awhile to dump the data.
// This seems to be a good number.
constexpr mx_time_t run_timeout = MX_SEC(10);

// Return the next sequence number to use or -1 if we've created the maximum
// number of dumps and can't make any more.

static int next_free_seq_num() {
    char test_file[sizeof(pt_path_prefix) + 10 + sizeof(pt_file_test_suffix)];

    for (int i = 0; i < kMaxIptDumps; ++i) {
        int seq = (i + next_seq_num) % kMaxIptDumps;
        snprintf(test_file, sizeof(test_file), PT_PATH_FORMAT ".%s",
                 pt_path_prefix, seq, pt_file_test_suffix);
        if (access(test_file, F_OK) < 0)
            return seq;
    }

    return -1;
}

static mx_status_t crashlogger_run(const char* name, int argc, const char* const* argv) {
    launchpad_t *lp;
    const char* executable = argv[0];
    launchpad_create(MX_HANDLE_INVALID, name, &lp);
    launchpad_load_from_file(lp, executable);
    launchpad_set_args(lp, argc, argv);
    launchpad_clone(lp, LP_CLONE_ALL);

    mx_handle_t child;
    const char* errmsg;
    mx_status_t status = launchpad_go(lp, &child, &errmsg);
    if (status != MX_OK)
        return status;

    mx_signals_t signals;
    status = mx_object_wait_one(child, MX_TASK_TERMINATED, mx_deadline_after(run_timeout),
                                &signals);
    if (status != MX_OK) {
        // Leave reporting the error to the caller.
    } else {
        if (signals & MX_TASK_TERMINATED) {
            mx_info_process_t info;
            status = mx_object_get_info(child, MX_INFO_PROCESS, &info,
                                        sizeof(info), nullptr, nullptr);
            if (status == MX_OK && info.exited) {
                if (info.return_code != 0) {
                    // The child should have already printed its own error
                    // message, we just need to return some error code to the
                    // caller
                    status = MX_ERR_IO;
                }
            } else {
                // This shouldn't happen, but we don't want to kill crashlogger
                // because of it. Return some indicative error code and let the
                // caller report it.
                status = MX_ERR_BAD_STATE;
            }
        } else {
            // This shouldn't happen, but we don't want to kill crashlogger
            // because of it. Return some indicative error code and let the
            // caller report it.
            status = MX_ERR_BAD_STATE;
        }
    }

    mx_handle_close(child);
    return status;
}

void try_dump_pt_data() {
    printf("Hi, this is try_dump_pt_data\n");
    if (access(ipt_program, F_OK) != 0) {
        // We only get called if dumping ipt is enabled.
        // Thus it's not noise to print a warning here.
        printf("Unable to dump PT data, missing PT control program: %s\n", ipt_program);
        return;
    }

    int seq_num = next_free_seq_num();
    if (seq_num < 0) {
        printf("Unable to dump IPT data, maximum number of dumps made.\n");
        printf("To re-enable dumps, delete old ones by removing %s.*.\n",
               pt_path_prefix);
        return;
    }

    constexpr char output_path_prefix_arg[] = "--output-path-prefix=";
    char full_output_path_prefix_arg[sizeof(output_path_prefix_arg) +
                                     sizeof(pt_path_prefix) + 10/*seq#*/];
    snprintf(full_output_path_prefix_arg, sizeof(full_output_path_prefix_arg),
             "%s" PT_PATH_FORMAT, output_path_prefix_arg,
             pt_path_prefix, seq_num);

    const char* const argv_pt_dump[] = {
        ipt_program,
        full_output_path_prefix_arg,
        "--verbose=2",
        "--control",
        "stop",
        "dump",
        "start",
    };
    mx_status_t status = crashlogger_run("ipt-dump",
                                         fbl::count_of(argv_pt_dump), argv_pt_dump);
    if (status == MX_OK) {
        printf("PT output written to " PT_PATH_FORMAT ".*\n",
               pt_path_prefix, seq_num);
    } else {
        print_mx_error("Error dumping IPT data", status);
    }

    // TODO(dje): It may be useful to break up the actions.
    // E.g., if the dump fails we still want to turn IPT back on.
}

#endif

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <limits.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct failure {
    list_node_t node;
    int cause;
    int rc;
    char name[0];
} failure_t;

static void fail_test(list_node_t* failures, const char* name, int cause, int rc) {
    size_t name_len = strlen(name) + 1;
    failure_t* failure = malloc(sizeof(failure_t) + name_len);
    failure->cause = cause;
    failure->rc = rc;
    memcpy(failure->name, name, name_len);
    list_add_tail(failures, &failure->node);
}

enum {
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
};

static int runtests(int argc, char** argv) {
    list_node_t failures = LIST_INITIAL_VALUE(failures);

    int total_count = 0;
    int failed_count = 0;

    const char* dirn = "/boot/test";
    DIR* dir = opendir(dirn);
    if (dir == NULL) {
        printf("error: cannot open '%s'\n", dirn);
        return -1;
    }

    // We want the default to be the same, whether the test is run by us
    // or run standalone. Do this by leaving the verbosity unspecified unless
    // provided by the user.
    int verbosity = -1;

    if (argc > 1) {
        if (strcmp(argv[1], "-q") == 0) {
            verbosity = 0;
        } else if (strcmp(argv[1], "-v") == 0) {
            printf("verbose output. enjoy.\n");
            verbosity = 1;
        } else {
            printf("unknown option. usage: %s [-q|-v]\n", argv[0]);
            return -1;
        }
    }

    struct dirent* de;
    struct stat stat_buf;
    while ((de = readdir(dir)) != NULL) {
        char name[11 + NAME_MAX + 1];
        snprintf(name, sizeof(name), "/boot/test/%s", de->d_name);
        if (stat(name, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        total_count++;
        if (verbosity) {
            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                de->d_name);
        }

        char verbose_opt[] = {'v','=', verbosity + '0', 0};
        const char* argv[] = {name, verbose_opt};
        int argc = verbosity >= 0 ? 2 : 1;

        mx_handle_t handle = launchpad_launch_mxio(name, argc, argv);
        if (handle < 0) {
            printf("FAILURE: Failed to launch %s: %d\n", de->d_name, handle);
            fail_test(&failures, de->d_name, FAILED_TO_LAUNCH, 0);
            failed_count++;
            continue;
        }

        mx_status_t status = mx_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                      MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR) {
            printf("FAILURE: Failed to wait for process exiting %s: %d\n", de->d_name, status);
            fail_test(&failures, de->d_name, FAILED_TO_WAIT, 0);
            failed_count++;
            continue;
        }

        // read the return code
        mx_info_process_t proc_info;
        mx_ssize_t info_status = mx_object_get_info(handle, MX_INFO_PROCESS, sizeof(proc_info.rec),
                &proc_info, sizeof(proc_info));
        mx_handle_close(handle);

        if (info_status != sizeof(proc_info)) {
            printf("FAILURE: Failed to get process return code %s: %" PRIdPTR "\n", de->d_name, info_status);
            fail_test(&failures, de->d_name, FAILED_TO_RETURN_CODE, 0);
            failed_count++;
            continue;
        }

        if (proc_info.rec.return_code == 0) {
            printf("PASSED: %s passed\n", de->d_name);
        } else {
            printf("FAILED: %s exited with nonzero status: %d\n", de->d_name, proc_info.rec.return_code);
            fail_test(&failures, de->d_name, FAILED_NONZERO_RETURN_CODE, proc_info.rec.return_code);
            failed_count++;
        }
    }

    closedir(dir);

    printf("\nSUMMARY: Ran %d tests: %d failed\n", total_count, failed_count);

    if (failed_count) {
        printf("\nThe following tests failed:\n");
        failure_t* failure = NULL;
        failure_t* temp = NULL;
        list_for_every_entry_safe (&failures, failure, temp, failure_t, node) {
            switch (failure->cause) {
            case FAILED_TO_LAUNCH:
                printf("%s: failed to launch\n", failure->name);
                break;
            case FAILED_TO_WAIT:
                printf("%s: failed to wait\n", failure->name);
                break;
            case FAILED_TO_RETURN_CODE:
                printf("%s: failed to return exit code\n", failure->name);
                break;
            case FAILED_NONZERO_RETURN_CODE:
                printf("%s: returned nonzero: %d\n", failure->name, failure->rc);
                break;
            }
            free(failure);
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    return runtests(argc, argv);
}

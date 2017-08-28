// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <limits.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <unittest/unittest.h>

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

static list_node_t failures = LIST_INITIAL_VALUE(failures);

static int total_count = 0;
static int failed_count = 0;

// We want the default to be the same, whether the test is run by us
// or run standalone. Do this by leaving the verbosity unspecified unless
// provided by the user.
static int verbosity = -1;

static const char* default_test_dirs[] = {
    "/boot/test/core", "/boot/test/libc", "/boot/test/ddk", "/boot/test/sys",
    "/boot/test/fs"
};
#define DEFAULT_NUM_TEST_DIRS 5

static bool run_tests(const char* dirn, const char* test_name) {
    DIR* dir = opendir(dirn);
    if (dir == NULL) {
        return false;
    }

    int init_failed_count = failed_count;
    struct dirent* de;
    struct stat stat_buf;
    while ((de = readdir(dir)) != NULL) {
        char name[64 + NAME_MAX];
        snprintf(name, sizeof(name), "%s/%s", dirn, de->d_name);
        if (stat(name, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        if ((test_name != NULL) && strncmp(test_name, de->d_name, NAME_MAX)) {
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

        launchpad_t* lp;
        launchpad_create(0, name, &lp);
        launchpad_load_from_file(lp, argv[0]);
        launchpad_clone(lp, LP_CLONE_ALL);
        launchpad_set_args(lp, argc, argv);
        const char* errmsg;
        mx_handle_t handle;
        mx_status_t status = launchpad_go(lp, &handle, &errmsg);
        if (status < 0) {
            printf("FAILURE: Failed to launch %s: %d: %s\n", de->d_name, status, errmsg);
            fail_test(&failures, de->d_name, FAILED_TO_LAUNCH, 0);
            failed_count++;
            continue;
        }

        status = mx_object_wait_one(handle, MX_PROCESS_TERMINATED,
                                    MX_TIME_INFINITE, NULL);
        if (status != MX_OK) {
            printf("FAILURE: Failed to wait for process exiting %s: %d\n", de->d_name, status);
            fail_test(&failures, de->d_name, FAILED_TO_WAIT, 0);
            failed_count++;
            continue;
        }

        // read the return code
        mx_info_process_t proc_info;
        status = mx_object_get_info(handle, MX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
        mx_handle_close(handle);

        if (status < 0) {
            printf("FAILURE: Failed to get process return code %s: %d\n", de->d_name, status);
            fail_test(&failures, de->d_name, FAILED_TO_RETURN_CODE, 0);
            failed_count++;
            continue;
        }

        if (proc_info.return_code == 0) {
            printf("PASSED: %s passed\n", de->d_name);
        } else {
            printf("FAILED: %s exited with nonzero status: %d\n", de->d_name, proc_info.return_code);
            fail_test(&failures, de->d_name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
            failed_count++;
        }
    }

    closedir(dir);
    return (init_failed_count == failed_count);
}

int usage(char* name) {
    fprintf(stderr,
            "usage: %s [-q|-v] [-S|-s] [-M|-m] [-L|-l] [-P|-p] [-a] [-t test name] [directories ...]\n"
            "\n"
            "The optional [directories ...] is a list of           \n"
            "directories containing tests to run, non-recursively. \n"
            "If not specified, the default set of directories is:  \n"
            "  /boot/test/core, /boot/test/libc, /boot/test/ddk,   \n"
            "  /boot/test/sys, /boot/test/fs                       \n"
            "\n"
            "options:                                              \n"
            "   -h: See this message                               \n"
            "   -v: Verbose output                                 \n"
            "   -q: Quiet output                                   \n"
            "   -S: Turn ON  Small tests         (on by default)   \n"
            "   -s: Turn OFF Small tests                           \n"
            "   -M: Turn ON  Medium tests        (on by default)   \n"
            "   -m: Turn OFF Medium tests                          \n"
            "   -L: Turn ON  Large tests         (off by default)  \n"
            "   -l: Turn OFF Large tests                           \n"
            "   -P: Turn ON Performance tests    (off by default)  \n"
            "   -p: Turn OFF Performance tests                     \n"
            "   -a: Turn on All tests                              \n", name);
    return -1;
}

int main(int argc, char** argv) {
    test_type_t test_type = TEST_DEFAULT;
    const char* test_name = NULL;
    int num_test_dirs = 0;
    const char** test_dirs = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-q") == 0) {
            verbosity = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            printf("verbose output. enjoy.\n");
            verbosity = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            test_type &= ~TEST_SMALL;
        } else if (strcmp(argv[i], "-m") == 0) {
            test_type &= ~TEST_MEDIUM;
        } else if (strcmp(argv[i], "-l") == 0) {
            test_type &= ~TEST_LARGE;
        } else if (strcmp(argv[i], "-p") == 0) {
            test_type &= ~TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-S") == 0) {
            test_type |= TEST_SMALL;
        } else if (strcmp(argv[i], "-M") == 0) {
            test_type |= TEST_MEDIUM;
        } else if (strcmp(argv[i], "-L") == 0) {
            test_type |= TEST_LARGE;
        } else if (strcmp(argv[i], "-P") == 0) {
            test_type |= TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-a") == 0) {
            test_type |= TEST_ALL;
        } else if (strcmp(argv[i], "-h") == 0) {
            return usage(argv[0]);
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                test_name = argv[i + 1];
                i++;
            } else {
                return usage(argv[0]);
            }
        } else if (argv[i][0] != '-') {
            num_test_dirs = argc - i;
            test_dirs = (const char**)&argv[i];
            break;
        } else {
            return usage(argv[0]);
        }
        i++;
    }

    // Configure the 'class' of tests which are meant to be executed by putting
    // it in an environment variable. Test executables can consume this environment
    // variable and process it as they would like.
    char test_opt[32];
    snprintf(test_opt, sizeof(test_opt), "%u", test_type);
    if (setenv(TEST_ENV_NAME, test_opt, 1) != 0) {
        printf("Failed: Could not set %s environment variable\n", TEST_ENV_NAME);
        return -1;
    }

    if (test_dirs == NULL) {
        test_dirs = default_test_dirs;
        num_test_dirs = DEFAULT_NUM_TEST_DIRS;
    }

    bool success = true;
    struct stat st;
    for (i = 0; i < num_test_dirs; i++) {
        if (stat(test_dirs[i], &st) < 0) {
            printf("Failed: Could not open %s\n", test_dirs[i]);
            return -1;
        }
        if (!S_ISDIR(st.st_mode)) {
            printf("Failed: %s is not a directory\n", test_dirs[i]);
            return -1;
        }

        // Don't continue running tests if one directory failed.
        success = run_tests(test_dirs[i], test_name);
        if (!success) {
            break;
        }
    }

    // It's not catastrophic if we can't unset it; we're just trying to clean up
    unsetenv(TEST_ENV_NAME);

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

    return failed_count ? 1 : 0;
}

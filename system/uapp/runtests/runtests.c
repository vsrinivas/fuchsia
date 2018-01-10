// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <libgen.h>
#include <limits.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static bool parse_test_names(char* input, char*** output, int* output_len) {
    // Count number of names via delimiter ','.
    int num_test_names = 0;
    for (char* tmp = input; tmp != NULL; tmp = strchr(tmp, ',')) {
        num_test_names++;
        tmp++;
    }

    // Allocate space for names.
    char** test_names = (char**) malloc(sizeof(char*) * num_test_names);
    if (test_names == NULL) {
        return false;
    }

    // Tokenize the input string into names.
    char *next_token;
    test_names[0] = strtok_r(input, ",", &next_token);
    for (int i = 1; i < num_test_names; i++) {
        char* tmp = strtok_r(NULL, ",", &next_token);
        if (tmp == NULL) {
            free(test_names);
            return false;
        }
        test_names[i] = tmp;
    }
    *output = test_names;
    *output_len = num_test_names;
    return true;
}

static bool match_test_names(const char* dirent_name, const char** test_names,
                             const int num_test_names) {
    // Always match when there are no test names to filter by.
    if (num_test_names <= 0) {
        return true;
    }
    for (int i = 0; i < num_test_names; i++) {
        if (!strncmp(test_names[i], dirent_name, NAME_MAX)) {
            return true;
        }
    }
    return false;
}

// Ensures a directory exists by creating it and its parents if it doesn't.
static int mkdir_all(const char* dirn) {
    char dir[PATH_MAX];
    size_t bytes_to_copy = strlcpy(dir, dirn, sizeof(dir));
    if (bytes_to_copy >= sizeof(dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Fast path: check if the directory already exists.
    struct stat s;
    if (!stat(dir, &s)) {
        return 0;
    }

    // Slow path: create the directory and its parents.
    for (size_t slash = 0u; dir[slash]; slash++) {
        if (slash != 0u && dir[slash] == '/') {
            dir[slash] = '\0';
            if (mkdir(dir, 0755) && errno != EEXIST) {
                return -1;
            }
            dir[slash] = '/';
        }
    }
    if (mkdir(dir, 0755) && errno != EEXIST) {
        return -1;
    }
    return 0;
}

// Invokes a test binary and prints results.
//
// |path| specifies the path to the binary.
// |name| is the human-friendly name of the test, for output.
// |outfd| is a file descriptor and if non-negative, will be used as the
//         stdout and stderr of the test binary.
//
// Returns true if the test binary successfully executes and has a return code of zero.
static bool run_test(const char* path, const char* name, const int outfd) {
    char verbose_opt[] = {'v','=', verbosity + '0', 0};
    const char* argv[] = {path, verbose_opt};
    int argc = verbosity >= 0 ? 2 : 1;

    launchpad_t* lp;
    launchpad_create(0, path, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_clone(lp, LP_CLONE_ALL);
    if (outfd >= 0) {
        launchpad_clone_fd(lp, outfd, STDOUT_FILENO);
        launchpad_transfer_fd(lp, outfd, STDERR_FILENO);
    }
    launchpad_set_args(lp, argc, argv);
    const char* errmsg;
    zx_handle_t handle;
    zx_status_t status = launchpad_go(lp, &handle, &errmsg);
    if (status < 0) {
        printf("FAILURE: Failed to launch %s: %d: %s\n", name, status, errmsg);
        fail_test(&failures, name, FAILED_TO_LAUNCH, 0);
        return false;
    }

    status = zx_object_wait_one(handle, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        printf("FAILURE: Failed to wait for process exiting %s: %d\n", name, status);
        fail_test(&failures, name, FAILED_TO_WAIT, 0);
        return false;
    }

    // read the return code
    zx_info_process_t proc_info;
    status = zx_object_get_info(handle, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
    zx_handle_close(handle);

    if (status < 0) {
        printf("FAILURE: Failed to get process return code %s: %d\n", name, status);
        fail_test(&failures, name, FAILED_TO_RETURN_CODE, 0);
        return false;
    }

    if (proc_info.return_code != 0) {
        printf("FAILED: %s exited with nonzero status: %d\n", name, proc_info.return_code);
        fail_test(&failures, name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
        return false;
    }

    printf("PASSED: %s passed\n", name);
    return true;
}

static bool run_tests(const char* dirn, const char** test_names, const int num_test_names,
                      const char* output_dir, FILE* summary_json) {
    DIR* dir = opendir(dirn);
    if (dir == NULL) {
        return false;
    }

    char abs_dirn[PATH_MAX];
    if (realpath(dirn, abs_dirn) == NULL) {
        printf("FAILURE: Could not resolve path %s: %s\n", dirn, strerror(errno));
        return false;
    }

    char test_output_dir[PATH_MAX];
    if (output_dir != NULL) {
        // Resolve an absolute path to the test directory to ensure output
        // directory names will never collide.
        size_t path_len = snprintf(test_output_dir, sizeof(test_output_dir), "%s/%s",
                                   output_dir, abs_dirn);
        if (path_len >= sizeof(test_output_dir)) {
            printf("FAILURE: Output path is too long %s/%s\n", output_dir, dirn);
            return false;
        }
        if (mkdir_all(test_output_dir)) {
            printf("FAILURE: Failed to create output directory %s: %s\n",
                   test_output_dir, strerror(errno));
            return false;
        }
    } else {
        test_output_dir[0] = '\0';
    }

    int init_failed_count = failed_count;
    int test_count = 0;
    struct dirent* de;
    struct stat stat_buf;
    while ((de = readdir(dir)) != NULL) {
        char name[64 + NAME_MAX];
        snprintf(name, sizeof(name), "%s/%s", abs_dirn, de->d_name);
        if (stat(name, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }
        if (!match_test_names(de->d_name, test_names, num_test_names)) {
            continue;
        }

        if (summary_json != NULL && test_count != 0) {
            fprintf(summary_json, ",\n");
        }

        test_count++;
        if (verbosity) {
            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                de->d_name);
        }

        int outfd = -1;
        if (output_dir != NULL) {
            char output_path[PATH_MAX];

            // Generate output file name.
            size_t path_len = snprintf(output_path, sizeof(output_path), "%s/%s.out",
                                       test_output_dir, de->d_name);
            if (path_len >= sizeof(output_path)) {
                printf("FAILURE: Output path is too long %s/%s\n", output_dir, dirn);
                continue;
            }

            // Open output file.
            outfd = open(output_path, O_CREAT | O_WRONLY | O_APPEND, 0664);
            if (outfd < 0) {
                printf("FAILURE: Failed to open output file %s: %s\n", output_path,
                       strerror(errno));
                continue;
            }
        }
        bool success = run_test(name, de->d_name, outfd);

        if (summary_json != NULL) {
            fprintf(summary_json, "{"
                    "\"name\": \"%s\","
                    "\"result\": \"%s\""
                    "}", name, success ? "PASS" : "FAIL");
        }

        if (!success) {
            failed_count++;
        }
    }

    closedir(dir);
    total_count += test_count;
    return (init_failed_count == failed_count);
}

int usage(char* name) {
    fprintf(stderr,
            "usage: %s [-q|-v] [-S|-s] [-M|-m] [-L|-l] [-P|-p] [-a]"
            " [-t test names] [-o directory] [directories ...]\n"
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
            "   -a: Turn on All tests                              \n"
            "   -t: Filter tests by name                           \n"
            "       (accepts a comma-separated list)               \n"
            "   -o: Write test output to a directory               \n"
            "\n"
            "If -o is enabled, then a JSON summary of the test     \n"
            "results will be written to a file named 'summary.json'\n"
            "under the desired directory, in addition to each      \n"
            "test's standard output and error.                     \n"
            "The summary contains a listing of the tests executed  \n"
            "by full path (e.g. /boot/test/core/futex_test) as well\n"
            "as whether the test passed or failed. For details, see\n"
            "//system/uapp/runtests/summary-schema.json            \n", name);
    return -1;
}

int main(int argc, char** argv) {
    test_type_t test_type = TEST_DEFAULT;
    int num_test_names = 0;
    const char** test_names = NULL;
    int num_test_dirs = 0;
    const char** test_dirs = NULL;
    const char* output_dir = NULL;

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
            if (i + 1 >= argc) {
                return usage(argv[0]);
            } else if (!parse_test_names(argv[i + 1], (char***)&test_names,
                                         &num_test_names)) {
                printf("Failed: Could not parse test names\n");
                return -1;
            }
            i++;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                return usage(argv[0]);
            }
            output_dir = (const char*)argv[i + 1];
            i++;
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

    struct stat st;
    if (output_dir != NULL && stat(output_dir, &st) < 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
        printf("Failed: Could not open %s\n", output_dir);
        return -1;
    }

    // Create a summary JSON file if there's an output directory.
    //
    // The summary JSON file is useful primarily for providing a
    // machine-readable summary of test results, and to allow bots to access
    // test output without having to enumerate a directory.
    FILE* summary_json = NULL;
    if (output_dir != NULL) {
        char summary_path[64 + NAME_MAX];
        snprintf(summary_path, sizeof(summary_path), "%s/summary.json", output_dir);
        summary_json = fopen(summary_path, "w");
        if (summary_json != NULL) {
            fprintf(summary_json, "{\"tests\": [\n");
        } else {
            printf("Failed: Could not create file %s\n", summary_path);
            return -1;
        }
    }

    bool success = true;
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
        success = run_tests(test_dirs[i], test_names, num_test_names, output_dir,
                            summary_json);
        if (!success) {
            break;
        }
    }
    free(test_names);

    // It's not catastrophic if we can't unset it; we're just trying to clean up
    unsetenv(TEST_ENV_NAME);

    if (output_dir != NULL) {
        // Close summary JSON file.
        fprintf(summary_json, "]}\n");
        if (fclose(summary_json)) {
            printf("Failed: Could not close 'summary.json'.\n");
            return -1;
        }

        // Sync output filesystem.
        int fd = open(output_dir, O_RDONLY);
        if (fd < 0) {
            printf("Failed: Could not open %s", output_dir);
        } else if (syncfs(fd)) {
            printf("Warning: Could not sync parent filesystem of %s", output_dir);
        } else {
            close(fd);
        }
    }

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

    // Print this last, since some infra recipes will shut down the fuchsia
    // environment once it appears.
    printf("\nSUMMARY: Ran %d tests: %d failed\n", total_count, failed_count);

    return failed_count ? 1 : 0;
}

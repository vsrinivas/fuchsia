// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/runtests-utils.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

// The name of the file containing stdout and stderr of a test.
constexpr char kOutputFileName[] = "stdout-and-stderr.txt";

// Ignore test directories where the last component is this. This permits users
// to specify a more general glob that might match to a subdirectory
// containing data for a particular test, which would result in failure should
// runtests try to enter it.
constexpr char kIgnoreDirName[] = "helper";

int Usage(const char* name, const fbl::Vector<fbl::String>& default_test_dirs) {
    bool test_dirs_required = default_test_dirs.is_empty();
    fprintf(stderr,
            "Usage: %s [-q|-v] [-S|-s] [-M|-m] [-L|-l] [-P|-p] [-a]\n"
            "    [-w timeout] [-t test names] [-o directory]       \n"
            "    [directory globs ...]                             \n"
            "\n"
            "The %s [directory globs...] is a list of        \n"
            "globs which match directories containing tests to run,\n"
            "non-recursively. Note that non-directories captured by\n"
            "a glob will be silently ignored.                      \n",
            name, test_dirs_required ? "required" : "optional");
    if (!test_dirs_required) {
        fprintf(stderr,
                "If unspecified, the default set of directories is\n");
        for (const auto& test_dir : default_test_dirs) {
            fprintf(stderr, "   %s\n", test_dir.c_str());
        }
    }
    fprintf(stderr,
            "\noptions:                                            \n"
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
            "   -w: Watchdog timeout                               \n"
            "       (accepts the timeout value in seconds)         \n"
            "       The default is up to each test.                \n"
            "\n"
            "If -o is enabled, then a JSON summary of the test     \n"
            "results will be written to a file named 'summary.json'\n"
            "under the desired directory, in addition to each      \n"
            "test's standard output and error.                     \n"
            "The summary contains a listing of the tests executed  \n"
            "by full path (e.g., /boot/test/core/futex_test), as   \n"
            "well as whether the test passed or failed. For        \n"
            "details, see                                          \n"
            "//system/ulib/runtests-utils/summary-schema.json      \n"
            "\n"
            "The test selection options -[sSmMlLpP] only work for  \n"
            "tests that support the RUNTESTS_TEST_CLASS environment\n"
            "variable.                                             \n"
            "The watchdog timeout option -w only works for tests   \n"
            "that support the RUNTESTS_WATCHDOG_TIMEOUT environment\n"
            "variable.                                             \n");
    return EXIT_FAILURE;
}
} // namespace


// TODO(IN-478): Split this function up into smaller functions.
int RunAllTests(const RunTestFn& RunTest, int argc, const char* const* argv,
                const fbl::Vector<fbl::String>& default_test_dirs,
                Stopwatch* stopwatch, const fbl::StringPiece syslog_file_name) {
    unsigned int test_types = TEST_DEFAULT;
    fbl::Vector<fbl::String> filter_names;
    fbl::Vector<fbl::String> test_globs;
    const char* output_dir = nullptr;
    signed char verbosity = -1;
    int watchdog_timeout_seconds = -1;

    // TODO(IN-478): Convert this logic to use getopt_long().
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-q") == 0) {
            verbosity = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            printf("verbose output. enjoy.\n");
            verbosity = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            test_types &= ~TEST_SMALL;
        } else if (strcmp(argv[i], "-m") == 0) {
            test_types &= ~TEST_MEDIUM;
        } else if (strcmp(argv[i], "-l") == 0) {
            test_types &= ~TEST_LARGE;
        } else if (strcmp(argv[i], "-p") == 0) {
            test_types &= ~TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-S") == 0) {
            test_types |= TEST_SMALL;
        } else if (strcmp(argv[i], "-M") == 0) {
            test_types |= TEST_MEDIUM;
        } else if (strcmp(argv[i], "-L") == 0) {
            test_types |= TEST_LARGE;
        } else if (strcmp(argv[i], "-P") == 0) {
            test_types |= TEST_PERFORMANCE;
        } else if (strcmp(argv[i], "-a") == 0) {
            test_types |= TEST_ALL;
        } else if (strcmp(argv[i], "-h") == 0) {
            return Usage(argv[0], default_test_dirs);
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                return Usage(argv[0], default_test_dirs);
            }
            ParseTestNames(argv[i + 1], &filter_names);
            i++;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                return Usage(argv[0], default_test_dirs);
            }
            output_dir = (const char*)argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-w") == 0) {
            if (i + 1 >= argc) {
                return Usage(argv[0], default_test_dirs);
            }
            const char* timeout_str = argv[++i];
            char* end;
            long timeout = strtol(timeout_str, &end, 0);
            if (*timeout_str == '\0' || *end != '\0' || timeout < 0 || timeout > INT_MAX) {
                fprintf(stderr, "Error: bad timeout\n");
                return EXIT_FAILURE;
            }
            watchdog_timeout_seconds = static_cast<int>(timeout);
        } else if (argv[i][0] != '-') {
            // Treat the rest of the argv array as a list of directory globs.
            while (i < argc) {
                test_globs.push_back(argv[i++]);
            }
            break;
        } else {
            return Usage(argv[0], default_test_dirs);
        }
        i++;
    }

    // Configure the types of tests which are meant to be executed by putting
    // it in an environment variable. Test executables can consume this environment
    // variable and process it as they would like.
    char test_opt[32];
    snprintf(test_opt, sizeof(test_opt), "%u", test_types);
    if (setenv(TEST_ENV_NAME, test_opt, 1) != 0) {
        printf("Error: Could not set %s environment variable\n", TEST_ENV_NAME);
        return EXIT_FAILURE;
    }

    // If set, configure the watchdog timeout to use.
    if (watchdog_timeout_seconds >= 0) {
        char timeout_str[32];
        snprintf(timeout_str, sizeof(timeout_str), "%d", watchdog_timeout_seconds);
        if (setenv(WATCHDOG_ENV_NAME, timeout_str, 1) != 0) {
            printf("Error: Could not set %s environment variable\n", WATCHDOG_ENV_NAME);
            return EXIT_FAILURE;
        }
    } else {
        // Ensure we don't pass on any existing value. This is intentional:
        // If -w is not specified then that means the watchdog is unspecified,
        // period.
        unsetenv(WATCHDOG_ENV_NAME);
    }

    // If we got no test globs, just set it to the default test dirs so we can
    // use glob patterns there too.
    if (test_globs.is_empty()) {
        if (default_test_dirs.is_empty()) {
            fprintf(stderr, "Test directory globs or default test directories must be specified.");
            return EXIT_FAILURE;
        }
        for (const fbl::String& test_dir : default_test_dirs) {
            test_globs.push_back(test_dir);
        }
    }

    // Takes test_globs and resolves them, putting the result in test_dirs, which
    // is used by the rest of the code.
    fbl::Vector<fbl::String> test_dirs;
    const int error = ResolveGlobs(test_globs, &test_dirs);
    if (error) {
        printf("Error: Failed to resolve globs, error = %d\n", error);
        return EXIT_FAILURE;
    }
    // TODO(mknyszek): Sort test_dirs in order to make running tests more
    // deterministic.
    struct stat st;
    if (output_dir != nullptr && stat(output_dir, &st) < 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
        printf("Error: Could not open %s\n", output_dir);
        return EXIT_FAILURE;
    }

    stopwatch->Start();
    int failed_count = 0;
    fbl::Vector<fbl::unique_ptr<Result>> results;
    for (const fbl::String& test_dir : test_dirs) {
        // In the event of failures around a directory not existing or being an empty node
        // we will continue to the next entries rather than aborting. This allows us to handle
        // different sets of default test directories.
        if (stat(test_dir.c_str(), &st) < 0) {
            printf("Could not open %s, skipping...\n", test_dir.c_str());
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            // Silently skip non-directories, as they may have been picked up in
            // the glob.
            continue;
        }

        // Resolve an absolute path to the test directory to ensure output
        // directory names will never collide.
        char abs_test_dir[PATH_MAX];
        if (realpath(test_dir.c_str(), abs_test_dir) == nullptr) {
            printf("Error: Could not resolve path %s: %s\n", test_dir.c_str(), strerror(errno));
            continue;
        }

        // Silently skip |kIgnoreDirName|.
        // The user may have done something like runtests /foo/bar/h*.
        const auto test_dir_base = basename(abs_test_dir);
        if (strcmp(test_dir_base, kIgnoreDirName) == 0) {
            continue;
        }

        // Ensure the output directory for this test binary's output exists.
        if (output_dir != nullptr) {
            char buf[PATH_MAX];
            size_t path_len = snprintf(buf, sizeof(buf), "%s/%s", output_dir, abs_test_dir);
            if (path_len >= sizeof(buf)) {
                printf("Error: Output path is too long: %s/%s\n", output_dir, abs_test_dir);
                return EXIT_FAILURE;
            }
            const int error = MkDirAll(buf);
            if (error) {
                printf("Error: Could not create output directory %s: %s\n", buf, strerror(error));
                return EXIT_FAILURE;
            }
        }

        int num_failed = 0;
        RunTestsInDir(RunTest, test_dir, filter_names, output_dir,
                      kOutputFileName, verbosity, &num_failed, &results);
        failed_count += num_failed;
    }

    // It's not catastrophic if we can't unset it; we're just trying to clean up
    unsetenv(TEST_ENV_NAME);
    unsetenv(WATCHDOG_ENV_NAME);

    if (output_dir != nullptr) {
        char summary_path[PATH_MAX];
        snprintf(summary_path, sizeof(summary_path), "%s/summary.json", output_dir);
        FILE* summary_json = fopen(summary_path, "w");
        if (summary_json == nullptr) {
            printf("Error: Could not open JSON summary file.\n");
            return EXIT_FAILURE;
        }
        const int error = WriteSummaryJSON(results, kOutputFileName,
                                           syslog_file_name, summary_json);
        if (error) {
            printf("Error: Failed to write JSON summary: %s\n", strerror(error));
            return EXIT_FAILURE;
        }
        if (fclose(summary_json)) {
            printf("Error: Could not close JSON summary.\n");
            return EXIT_FAILURE;
        }

        // Sync output filesystem.
        // disable on Mac until proper way to sync Mac filesystems is discovered
#if !defined(__APPLE__)
        int fd = open(output_dir, O_RDONLY);
        if (fd < 0) {
            printf("Warning: Could not open %s for syncing", output_dir);
        } else if (syncfs(fd)) {
            printf("Warning: Could not sync parent filesystem of %s", output_dir);
        } else {
            close(fd);
        }
#endif
    }

    // Display any failed tests, and free the test results.
    if (failed_count) {
        printf("\nThe following tests failed:\n");
    }
    for (const fbl::unique_ptr<Result>& result : results) {
        switch (result->launch_status) {
        case SUCCESS:
            break;
        case FAILED_TO_LAUNCH:
            printf("%s: failed to launch\n", result->name.c_str());
            break;
        case FAILED_TO_WAIT:
            printf("%s: failed to wait\n", result->name.c_str());
            break;
        case FAILED_TO_RETURN_CODE:
            printf("%s: failed to return exit code\n", result->name.c_str());
            break;
        case FAILED_NONZERO_RETURN_CODE:
            printf("%s: returned nonzero: %" PRId64 "\n",
                   result->name.c_str(), result->return_code);
            break;
        default:
            printf("%s: unknown result\n", result->name.c_str());
            break;
        }
    }

    // TODO(ZX-2051): Include total duration in summary.json.
    uint64_t time_taken_ms = stopwatch->DurationInMsecs();

    // Print this last, since some infra recipes will shut down the fuchsia
    // environment once it appears.
    printf("\nSUMMARY: Ran %lu tests: %d failed (%" PRIu64 ".%03u sec)\n", results.size(),
           failed_count, time_taken_ms / 1000, (unsigned)(time_taken_ms % 1000));

    return failed_count ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace runtests

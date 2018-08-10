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
            "   -f: Run tests specified in this file               \n"
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
            "variable.                                             \n"
            "-f and [directory globs ...] are mutually exclusive.  \n");
    return EXIT_FAILURE;
}

// Trying to accomplish the same thing as syncfs() but using only POSIX.
// A single call to fsync() only has to do with the data for that file, but that file may be missing
// from the directories above it.
void SyncPathAndAncestors(const char* path) {
    // dirname mutates its argument.
    char mutable_path[PATH_MAX];
    strncpy(mutable_path, path, PATH_MAX - 1);
    mutable_path[PATH_MAX - 1] = '\0';
    for (char* p = mutable_path; ; p = dirname(p)) {
        int fd = open(p, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Warning: Could not open %s for syncing: %s", p, strerror(errno));
            return;
        } else if (fsync(fd)) {
            fprintf(stderr, "Warning: Could not sync %s: %s", p, strerror(errno));
            return;
        } else if (close(fd)) {
            fprintf(stderr, "Warning: Could not close %s: %s", p, strerror(errno));
            return;
        }
        if (!strcmp(p, "/")) break;
    }
};
} // namespace

int DiscoverAndRunTests(const RunTestFn& RunTest, int argc, const char* const* argv,
                        const fbl::Vector<fbl::String>& default_test_dirs, Stopwatch* stopwatch,
                        const fbl::StringPiece syslog_file_name) {
    unsigned int test_types = TEST_DEFAULT;
    fbl::Vector<fbl::String> basename_whitelist;
    fbl::Vector<fbl::String> test_dir_globs;
    const char* output_dir = nullptr;
    signed char verbosity = -1;
    int watchdog_timeout_seconds = -1;
    const char* test_list_path = nullptr;

    int c;
    // getopt uses global state, reset it.
    optind = 1;
    // Starting with + means don't modify |argv|.
    static const char* kOptString = "+qvsmlpSMLPaht:o:f:w:";
    while ((c = getopt(argc, const_cast<char* const*>(argv), kOptString)) != -1) {
        switch (c) {
        case 'q':
            verbosity = 0;
            break;
        case 'v':
            fprintf(stderr, "verbose output. enjoy.\n");
            verbosity = 1;
            break;
        case 's':
            test_types &= ~TEST_SMALL;
            break;
        case 'm':
            test_types &= ~TEST_MEDIUM;
            break;
        case 'l':
            test_types &= ~TEST_LARGE;
            break;
        case 'p':
            test_types &= ~TEST_PERFORMANCE;
            break;
        case 'S':
            test_types |= TEST_SMALL;
            break;
        case 'M':
            test_types |= TEST_MEDIUM;
            break;
        case 'L':
            test_types |= TEST_LARGE;
            break;
        case 'P':
            test_types |= TEST_PERFORMANCE;
            break;
        case 'a':
            test_types |= TEST_ALL;
            break;
        case 'h':
            return Usage(argv[0], default_test_dirs);
        case 't':
            ParseTestNames(optarg, &basename_whitelist);
            break;
        case 'o':
            output_dir = optarg;
            break;
        case 'f':
            test_list_path = optarg;
            break;
        case 'w':
        {
            const char* timeout_str = optarg;
            char* end;
            long timeout = strtol(timeout_str, &end, 0);
            if (*timeout_str == '\0' || *end != '\0' || timeout < 0 || timeout > INT_MAX) {
                fprintf(stderr, "Error: bad timeout\n");
                return EXIT_FAILURE;
            }
            watchdog_timeout_seconds = static_cast<int>(timeout);
            break;
        }
        default:
            return Usage(argv[0], default_test_dirs);
        }
    }
    // Treat the rest of the argv array as a list of directory globs.
    for (int i = optind; i < argc; ++i) {
        test_dir_globs.push_back(argv[i]);
    }

    if (test_list_path && !test_dir_globs.is_empty()) {
        fprintf(stderr, "Can't set both -f and directory globs.\n");
        return Usage(argv[0], default_test_dirs);
    }

    // Configure the types of tests which are meant to be executed by putting
    // it in an environment variable. Test executables can consume this environment
    // variable and process it as they would like.
    char test_opt[32];
    snprintf(test_opt, sizeof(test_opt), "%u", test_types);
    if (setenv(TEST_ENV_NAME, test_opt, 1) != 0) {
        fprintf(stderr, "Error: Could not set %s environment variable\n", TEST_ENV_NAME);
        return EXIT_FAILURE;
    }

    // If set, configure the watchdog timeout to use.
    if (watchdog_timeout_seconds >= 0) {
        char timeout_str[32];
        snprintf(timeout_str, sizeof(timeout_str), "%d", watchdog_timeout_seconds);
        if (setenv(WATCHDOG_ENV_NAME, timeout_str, 1) != 0) {
            fprintf(stderr, "Error: Could not set %s environment variable\n", WATCHDOG_ENV_NAME);
            return EXIT_FAILURE;
        }
    } else {
        // Ensure we don't pass on any existing value. This is intentional:
        // If -w is not specified then that means the watchdog is unspecified,
        // period.
        unsetenv(WATCHDOG_ENV_NAME);
    }

    fbl::Vector<fbl::String> test_paths;
    const auto* test_dir_globs_or_default =
        test_dir_globs.is_empty() ? &default_test_dirs : &test_dir_globs;
    if (test_list_path) {
        FILE* test_list_file = fopen(test_list_path, "r");
        if (!test_list_file) {
            fprintf(stderr, "Failed to open test list file %s: %s\n", test_list_path,
                    strerror(errno));
            return false;
        }
        const int err = DiscoverTestsInListFile(test_list_file, &test_paths);
        fclose(test_list_file);
        if (err) {
            fprintf(stderr, "Failed to read test list from %s: %s\n", test_list_path,
                    strerror(err));
            return EXIT_FAILURE;
        }
    } else if (!test_dir_globs_or_default->is_empty()) {
        const int err = DiscoverTestsInDirGlobs(*test_dir_globs_or_default, kIgnoreDirName,
                                                basename_whitelist, &test_paths);
        if (err) {
            fprintf(stderr, "Failed to find tests in dirs: %s\n", strerror(err));
            return EXIT_FAILURE;
        }
    } else {
        fprintf(
            stderr,
            "Test list path, test directory globs or default test directories must be specified.");
        return EXIT_FAILURE;
    }


    struct stat st;
    if (output_dir != nullptr && stat(output_dir, &st) < 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
        fprintf(stderr, "Error: Could not open %s\n", output_dir);
        return EXIT_FAILURE;
    }

    // TODO(mknyszek): Sort test_paths for deterministic behavior. Should be easy after ZX-1751.
    stopwatch->Start();
    int failed_count = 0;
    fbl::Vector<fbl::unique_ptr<Result>> results;
    if (!RunTests(RunTest, test_paths, output_dir, kOutputFileName, verbosity, &failed_count,
                  &results)) {
        return EXIT_FAILURE;
    }

    // It's not catastrophic if we can't unset it; we're just trying to clean up
    unsetenv(TEST_ENV_NAME);
    unsetenv(WATCHDOG_ENV_NAME);

    if (output_dir != nullptr) {
        char summary_path[PATH_MAX];
        snprintf(summary_path, sizeof(summary_path), "%s/summary.json", output_dir);
        FILE* summary_json = fopen(summary_path, "w");
        if (summary_json == nullptr) {
            fprintf(stderr, "Error: Could not open JSON summary file.\n");
            return EXIT_FAILURE;
        }
        const int error = WriteSummaryJSON(results, kOutputFileName,
                                           syslog_file_name, summary_json);
        if (error) {
            fprintf(stderr, "Error: Failed to write JSON summary: %s\n", strerror(error));
            return EXIT_FAILURE;
        }
        if (fclose(summary_json)) {
            fprintf(stderr, "Error: Could not close JSON summary.\n");
            return EXIT_FAILURE;
        }

        SyncPathAndAncestors(output_dir);
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

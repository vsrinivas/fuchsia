// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>
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
          "    [-- [-args -to -the -test -bin]]                  \n"
          "\n"
          "The %s [directory globs...] is a list of        \n"
          "globs which match directories containing tests to run,\n"
          "non-recursively. Note that non-directories captured by\n"
          "a glob will be silently ignored.                      \n"
          "\n"
          "After directory globs, `--` can be followed by any    \n"
          "number of arguments to pass to the binaries under     \n"
          "test.\n",
          name, test_dirs_required ? "required" : "optional");
  if (!test_dirs_required) {
    fprintf(stderr, "If unspecified, the default set of directories is\n");
    for (const auto& test_dir : default_test_dirs) {
      fprintf(stderr, "   %s\n", test_dir.c_str());
    }
  }
  fprintf(stderr,
          "\noptions:                                            \n"
          "   -h: See this message                               \n"
          "   -d: Dry run, just print test file names and exit   \n"
          "   -v: Verbose output [1]                             \n"
          "   -q: Quiet output                                   \n"
          "   -S: Turn ON  Small tests      (on by default)  [2] \n"
          "   -s: Turn OFF Small tests                       [2] \n"
          "   -M: Turn ON  Medium tests     (on by default)  [2] \n"
          "   -m: Turn OFF Medium tests                      [2] \n"
          "   -L: Turn ON  Large tests      (off by default) [2] \n"
          "   -l: Turn OFF Large tests                       [2] \n"
          "   -P: Turn ON Performance tests (off by default) [2] \n"
          "   -p: Turn OFF Performance tests                 [2] \n"
          "   -a: Turn on All tests                              \n"
          "   -t: Filter tests found in directory globs by these \n"
          "       basenames. Also accepts fuchsia-pkg URIs, which\n"
          "       are run regardless of directory globs.         \n"
          "       (accepts a comma-separated list)               \n"
          "   -r: Repeat the test suite this many times          \n"
          "   -o: Write test output to a directory [4]           \n"
          "   -w: Watchdog timeout [5]                           \n"
          "       (accepts the timeout value in seconds)         \n"
          "       The default is up to each test.                \n"
          "   -i: Per-test timeout in seconds. [6]               \n"
          "\n"
          "[1] -v will pass \"v=1\" argument to the test binary. \n"
          "    Not all test frameworks will honor this argument, \n"
          "    and some, like Rust, may interpret it as a filter.\n"
          "    Use \"-- --nocapture\" for a similar behaviour    \n"
          "    when running Rust tests.                          \n"
          "\n"
          "[2] The test selection options -[sSmMlLpP] only work  \n"
          "    for tests that support the RUNTESTS_TEST_CLASS    \n"
          "    environment variable.                             \n"
          "\n"
          "[4] If -o is enabled, then a JSON summary of the test \n"
          "    results will be written to a file named           \n"
          "    \"summary.json\" under the desired directory, in  \n"
          "    addition to each test's standard output and error.\n"
          "    The summary contains a listing of the tests       \n"
          "    executed by full path (e.g.,                      \n"
          "    /boot/test/core/futex_test), as well as whether   \n"
          "    the test passed or failed. For details, see       \n"
          "    //system/ulib/runtests-utils/summary-schema.json  \n"
          "\n"
          "[5] The watchdog timeout option -w only works for     \n"
          "    tests that support the RUNTESTS_WATCHDOG_TIMEOUT  \n"
          "    environment variable.                             \n"
          "\n"
          "[6] Will consider tests failed if they don't          \n"
          "    finish in this time. If > 1, watchdog timeout     \n"
          "    will be set to (this value - 1) in order to give  \n"
          "    tests a chance to clean up and fail cleanly.      \n");
  return EXIT_FAILURE;
}
}  // namespace

int DiscoverAndRunTests(int argc, const char* const* argv,
                        const fbl::Vector<fbl::String>& default_test_dirs, Stopwatch* stopwatch,
                        const fbl::StringPiece syslog_file_name) {
  unsigned int test_types = TEST_DEFAULT;
  fbl::Vector<fbl::String> basename_whitelist;
  fbl::Vector<fbl::String> test_dir_globs;
  fbl::Vector<fbl::String> test_args;
  const char* output_dir = nullptr;
  signed char verbosity = -1;
  int watchdog_timeout_seconds = -1;
  unsigned int timeout_seconds = 0;
  int repeat = 1;
  bool dry_run = false;

  int optind = 1;
  while (optind < argc) {
    // Implementing our own opt parsing here is less effort that fixing up
    // the behavior across three different getopt implementations on macos,
    // linux and zircon, even with this comment. The breaking requirement is
    // to parse globs at any position in argv.
    fbl::String arg(argv[optind++]);

    if (arg.length() == 0) {
      continue;
    }

    if (arg == "--") {
      for (; optind < argc; ++optind) {
        test_args.push_back(argv[optind]);
      }
      break;
    }

    if (arg.length() < 2 || arg.data()[0] != '-') {
      test_dir_globs.push_back(std::move(arg));
      continue;
    }

    switch (arg.data()[1]) {
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
        if (optind > argc) {
          fprintf(stderr, "Missing argument for -t\n");
          return EXIT_FAILURE;
        }
        ParseTestNames(argv[optind++], &basename_whitelist);
        break;
      case 'o':
        if (optind > argc) {
          fprintf(stderr, "Missing argument for -o\n");
          return EXIT_FAILURE;
        }
        output_dir = argv[optind++];
        break;
      case 'r': {
        if (optind > argc) {
          fprintf(stderr, "Missing argument for -r\n");
          return EXIT_FAILURE;
        }
        const char* repeat_str = argv[optind++];
        char* end;
        long repeatl = strtol(repeat_str, &end, 0);
        if (*repeat_str == '\0' || *end != '\0' || repeatl < 0 || repeatl > INT_MAX) {
          fprintf(stderr, "Error: bad repeat\n");
          return EXIT_FAILURE;
        }
        repeat = static_cast<int>(repeatl);
        break;
      }
      case 'i':  // intentional fall-through
      case 'w': {
        if (optind > argc) {
          fprintf(stderr, "Missing argument for %s\n", arg.data());
          return EXIT_FAILURE;
        }
        const char* timeout_str = argv[optind++];
        char* end;
        long timeout = strtol(timeout_str, &end, 0);
        if (*timeout_str == '\0' || *end != '\0' || timeout < 0 || timeout > INT_MAX) {
          fprintf(stderr, "Error: bad timeout\n");
          return EXIT_FAILURE;
        }
        if (arg.data()[1] == 'w') {
          watchdog_timeout_seconds = static_cast<int>(timeout);
        } else {
          timeout_seconds = static_cast<unsigned int>(timeout);
          if (watchdog_timeout_seconds == -1 && timeout_seconds > 1 && timeout_seconds <= INT_MAX) {
            // Give tests a chance to exit cleanly before the timeout kills them.
            watchdog_timeout_seconds = static_cast<int>(timeout_seconds - 1);
          }
        }
        break;
      }
      case 'd': {
        dry_run = true;
        break;
      }
      default:
        return Usage(argv[0], default_test_dirs);
    }
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
  if (!test_dir_globs_or_default->is_empty()) {
    const int err = DiscoverTestsInDirGlobs(*test_dir_globs_or_default, kIgnoreDirName,
                                            basename_whitelist, &test_paths);
    if (err) {
      fprintf(stderr, "Failed to find tests in dirs: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
    CopyFuchsiaPkgURIs(basename_whitelist, &test_paths);
  } else {
    fprintf(stderr, "Test directory globs or default test directories must be specified.");
    return EXIT_FAILURE;
  }

  struct stat st;
  if (output_dir != nullptr && stat(output_dir, &st) < 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
    fprintf(stderr, "Error: Could not open %s\n", output_dir);
    return EXIT_FAILURE;
  }

  if (dry_run) {
    printf("Would run the following tests:\n");
    for (const auto& test_path : test_paths) {
      printf("\t%s\n", test_path.c_str());
    }
    return EXIT_SUCCESS;
  }

  std::sort(test_paths.begin(), test_paths.end());
  stopwatch->Start();
  int failed_count = 0;
  fbl::Vector<std::unique_ptr<Result>> results;
  if (!RunTests(test_paths, test_args, repeat,
                static_cast<uint64_t>(timeout_seconds) * static_cast<uint64_t>(1000), output_dir,
                kOutputFileName, verbosity, &failed_count, &results)) {
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
    const int error = WriteSummaryJSON(results, kOutputFileName, syslog_file_name, summary_json);
    if (error) {
      fprintf(stderr, "Error: Failed to write JSON summary: %s\n", strerror(error));
      return EXIT_FAILURE;
    }
    if (fclose(summary_json)) {
      fprintf(stderr, "Error: Could not close JSON summary.\n");
      return EXIT_FAILURE;
    }
  }

  // Display any failed tests, and free the test results.
  if (failed_count) {
    printf("\nThe following tests failed:\n");
  }
  for (const std::unique_ptr<Result>& result : results) {
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
        printf("%s: returned nonzero: %" PRId64 "\n", result->name.c_str(), result->return_code);
        break;
      case TIMED_OUT:
        printf("%s: timed out\n", result->name.c_str());
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

}  // namespace runtests

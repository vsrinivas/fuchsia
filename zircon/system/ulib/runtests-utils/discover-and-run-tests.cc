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
  fprintf(stderr,
          "Usage: %s [-S|-s] [-M|-m] [-L|-l] [--all]                \n"
          "    [--names|-n test names] [--output|-o directory]      \n"
          "    [test paths or URLs ...]                             \n"
          "    [-- [-args -to -the -test -bins]]                    \n"
          "                                                         \n"
          "After tests, `--` can be followed by any                 \n"
          "number of arguments to pass to all of the binaries under \n"
          "test.                                                    \n"
          "                                                         \n"
          "After test is run, a signature of [runtests][PASSED]     \n"
          "or [runtests][FAILED] will be printed                    \n"
          "                                                         \n"
          "If --all or --names|-n is passed, tests will be run from \n"
          "the default globs:                                       \n",
          name);
  for (const auto& test_dir : default_test_dirs) {
    fprintf(stderr, "\t%s\n", test_dir.c_str());
  }
  fprintf(stderr,
          "                                                         \n"
          "options:                                                 \n"
          "       -h: See this message                              \n"
          "       -d: Dry run, just print test file names and exit  \n"
          "       -i: Per-test timeout in seconds.            [2]   \n"
          "       -r: Repeat the test suite this many times         \n"
          "  --names: Filter tests found in the default directory   \n"
          "           globs by these basenames. Also accepts        \n"
          "           fuchsia-pkg URIs, which are run regardless    \n"
          "           of directory globs. (accepts a                \n"
          "           comma-separated list)                         \n"
          "       -n: Same as --names.                              \n"
          " --output: Write test output to a directory        [3]   \n"
          "       -o: Same as --output.                             \n"
          "    --all: Run tests found in the default directory      \n"
          "           globs.                                        \n"
          "                                                         \n"
          "[1] The test selection options -[sSmMlL] only work       \n"
          "    for tests that support the RUNTESTS_TEST_CLASS       \n"
          "    environment variable.                                \n"
          "                                                         \n"
          "[2] Will consider tests failed if they don't             \n"
          "    finish in this time.                                 \n"
          "                                                         \n"
          "[3] If -o is enabled, then a JSON summary of the test    \n"
          "    results will be written to a file named              \n"
          "    \"summary.json\" under the desired directory, in     \n"
          "    addition to each test's standard output and error.   \n"
          "    The summary contains a listing of the tests          \n"
          "    executed by full path (e.g.,                         \n"
          "    /boot/test/core/futex_test), as well as whether      \n"
          "    the test passed or failed. For details, see          \n"
          "    //system/ulib/runtests-utils/summary-schema.json     \n");
  return EXIT_FAILURE;
}
}  // namespace

int DiscoverAndRunTests(int argc, const char* const* argv,
                        const fbl::Vector<fbl::String>& default_test_dirs, Stopwatch* stopwatch,
                        const fbl::StringPiece syslog_file_name) {
  bool use_default_globs = false;
  fbl::Vector<fbl::String> basename_whitelist;
  fbl::Vector<fbl::String> test_args;
  fbl::Vector<fbl::String> test_paths;
  const char* output_dir = nullptr;
  unsigned int timeout_seconds = 0;
  int repeat = 1;
  bool dry_run = false;

  int optind = 1;
  while (optind < argc) {
    // Implementing our own opt parsing here is less effort than fixing up
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

    if (arg == "--all" || arg == "--names" || arg == "-n") {
      use_default_globs = true;
      if (arg == "--names" || arg == "-n") {
        if (optind > argc) {
          fprintf(stderr, "Missing argument for %s\n", arg.c_str());
          return EXIT_FAILURE;
        }
        ParseTestNames(argv[optind++], &basename_whitelist);
      }
      continue;
    }

    if (arg == "--output" || arg == "-o") {
      if (optind > argc) {
        fprintf(stderr, "Missing argument for %s\n", arg.c_str());
        return EXIT_FAILURE;
      }
      output_dir = argv[optind++];
      continue;
    }

    if (arg.data()[0] != '-') {
      test_paths.push_back(std::move(arg));
      continue;
    }

    switch (arg.data()[1]) {
      case 'h':
        return Usage(argv[0], default_test_dirs);
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
      case 'i': {
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
        timeout_seconds = static_cast<unsigned int>(timeout);
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

  if (use_default_globs) {
    const int err =
        DiscoverTestsInDirGlobs(default_test_dirs, kIgnoreDirName, basename_whitelist, &test_paths);
    if (err) {
      fprintf(stderr, "Failed to find tests in dirs: %s\n", strerror(err));
      return EXIT_FAILURE;
    }
  }

  if (test_paths.is_empty()) {
    fprintf(stderr, "No tests found or specified.\n");
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
                kOutputFileName, &failed_count, &results)) {
    return EXIT_FAILURE;
  }

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

  if (results.is_empty()) {
    printf("\nWARNING: 0 tests run.\n");
  } else if (results.size() > 1) {
    // In the the case of a single test, this information is already present in
    // the last line of output.
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

    // TODO(fxbug.dev/31913): Include total duration in summary.json.
    uint64_t time_taken_ms = stopwatch->DurationInMsecs();
    printf("\nSUMMARY: Ran %lu tests: %d failed (%" PRIu64 ".%03u sec)\n", results.size(),
           failed_count, time_taken_ms / 1000, (unsigned)(time_taken_ms % 1000));
  }

  return failed_count ? EXIT_FAILURE : EXIT_SUCCESS;
}

}  // namespace runtests

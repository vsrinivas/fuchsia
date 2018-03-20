// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/runner.h>

#include <errno.h>
#include <getopt.h>
#include <regex.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace perftest {
namespace {

// g_tests needs to be POD because this list is populated by constructors.
// We don't want g_tests to have a constructor that might get run after
// items have been added to the list, because that would clobber the list.
internal::TestList* g_tests;

class RepeatStateImpl : public RepeatState {
public:
    RepeatStateImpl(uint32_t run_count)
        : run_count_(run_count) {
        // Add 1 because we store timestamps for the start of each test run
        // (which serve as timestamps for the end of the previous test
        // run), plus one more timestamp for the end of the last test run.
        size_t array_size = run_count + 1;
        timestamps_.reset(new uint64_t[array_size]);
        // Clear the array in order to fault in the pages.  This should
        // prevent page faults occurring as we cross page boundaries when
        // writing a test's running times (which would affect the first
        // test case but not later test cases).
        memset(timestamps_.get(), 0, sizeof(timestamps_[0]) * array_size);
    }

    bool KeepRunning() override {
        timestamps_[runs_started_] = zx_ticks_get();
        if (unlikely(runs_started_ == run_count_)) {
            ++finishing_calls_;
            return false;
        }
        ++runs_started_;
        return true;
    }

    bool Success() const {
        return runs_started_ == run_count_ && finishing_calls_ == 1;
    }

    void CopyTimeResults(const char* test_name, ResultsSet* dest) const {
        // Copy the timing results, converting timestamps to elapsed times.
        double nanoseconds_per_tick =
            1e9 / static_cast<double>(zx_ticks_per_second());
        TestCaseResults* results = dest->AddTestCase(test_name, "nanoseconds");
        results->values()->reserve(run_count_);
        for (uint32_t idx = 0; idx < run_count_; ++idx) {
            uint64_t time_taken = timestamps_[idx + 1] - timestamps_[idx];
            results->AppendValue(
                static_cast<double>(time_taken) * nanoseconds_per_tick);
        }
    }

private:
    // Number of test runs that we intend to do.
    uint32_t run_count_;
    // Number of test runs started.
    uint32_t runs_started_ = 0;
    // Number of calls to KeepRunning() after the last test run has been
    // started.  This should be 1 when the test runs have finished.  This
    // is just used as a sanity check: It will be 0 if the test case failed
    // to make the final call to KeepRunning() or >1 if it made unnecessary
    // excess calls.
    //
    // Having this separate from runs_started_ removes the need for an
    // extra comparison in the fast path of KeepRunning().
    uint32_t finishing_calls_ = 0;
    fbl::unique_ptr<uint64_t[]> timestamps_;
};

}  // namespace

void RegisterTest(const char* name, fbl::Function<TestFunc> test_func) {
    if (!g_tests) {
        g_tests = new internal::TestList;
    }
    internal::NamedTest new_test{name, fbl::move(test_func)};
    g_tests->push_back(fbl::move(new_test));
}

namespace internal {

bool RunTests(TestList* test_list, uint32_t run_count, const char* regex_string,
              FILE* log_stream, ResultsSet* results_set) {
    // Compile the regular expression.
    regex_t regex;
    int err = regcomp(&regex, regex_string, REG_EXTENDED);
    if (err != 0) {
        char msg[256];
        msg[0] = '\0';
        regerror(err, &regex, msg, sizeof(msg));
        fprintf(log_stream,
                "Compiling the regular expression \"%s\" failed: %s\n",
                regex_string, msg);
        return false;
    }

    bool found_regex_match = false;
    bool ok = true;
    for (const internal::NamedTest& test_case : *test_list) {
        const char* test_name = test_case.name.c_str();
        bool matched_regex = regexec(&regex, test_name, 0, nullptr, 0) == 0;
        if (!matched_regex) {
            continue;
        }
        found_regex_match = true;

        // Log in a format similar to gtest's output, so that this will
        // look familiar to readers and to allow parsing by tools that can
        // parse gtest's output.
        fprintf(log_stream, "[ RUN      ] %s\n", test_name);

        RepeatStateImpl state(run_count);
        bool result = test_case.test_func(&state);

        if (!result) {
            fprintf(log_stream, "[  FAILED  ] %s\n", test_name);
            ok = false;
            continue;
        }
        if (!state.Success()) {
            fprintf(log_stream, "Excess or missing calls to KeepRunning()\n");
            fprintf(log_stream, "[  FAILED  ] %s\n", test_name);
            ok = false;
            continue;
        }
        fprintf(log_stream, "[       OK ] %s\n", test_name);

        state.CopyTimeResults(test_name, results_set);
    }

    regfree(&regex);

    if (!found_regex_match) {
        // Report an error so that this doesn't fail silently if the regex
        // is wrong.
        fprintf(log_stream,
                "The regular expression \"%s\" did not match any tests\n",
                regex_string);
        return false;
    }
    return ok;
}

void ParseCommandArgs(int argc, char** argv, CommandArgs* dest) {
    static const struct option opts[] = {
        {"out", required_argument, nullptr, 'o'},
        {"filter", required_argument, nullptr, 'f'},
        {"runs", required_argument, nullptr, 'r'},
    };
    optind = 1;
    for (;;) {
        int opt = getopt_long(argc, argv, "", opts, nullptr);
        if (opt < 0) {
            break;
        }
        switch (opt) {
        case 'o':
            dest->output_filename = optarg;
            break;
        case 'f':
            dest->filter_regex = optarg;
            break;
        case 'r': {
            // Convert string to number.
            char* end;
            long val = strtol(optarg, &end, 0);
            // Check that the string contains only a positive number and
            // that the number doesn't overflow.
            if (val != static_cast<uint32_t>(val) || *end != '\0' ||
                *optarg == '\0' || val == 0) {
                fprintf(stderr, "Invalid argument for --runs: \"%s\"\n",
                        optarg);
                exit(1);
            }
            dest->run_count = static_cast<uint32_t>(val);
            break;
        }
        default:
            // getopt_long() will have printed an error already.
            exit(1);
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Unrecognized argument: \"%s\"\n", argv[optind]);
        exit(1);
    }
}

}  // namespace internal

static bool PerfTestMode(int argc, char** argv) {
    internal::CommandArgs args;
    internal::ParseCommandArgs(argc, argv, &args);
    ResultsSet results;
    bool success = RunTests(g_tests, args.run_count, args.filter_regex, stdout,
                            &results);

    printf("\n");
    results.PrintSummaryStatistics(stdout);
    printf("\n");

    if (args.output_filename) {
        FILE* fh = fopen(args.output_filename, "w");
        if (!fh) {
            fprintf(stderr, "Failed to open output file \"%s\": %s\n",
                    args.output_filename, strerror(errno));
            exit(1);
        }
        results.WriteJSON(fh);
        fclose(fh);
    }

    return success;
}

int PerfTestMain(int argc, char** argv) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        printf("Usage:\n"
               "  %s -p [options]  # run in \"perf test mode\"\n"
               "  %s               # run in \"unit test mode\"\n"
               "\n"
               "\"Unit test mode\" runs perf tests as unit tests.  "
               "This means it only checks that the perf tests pass.  "
               "It only does a small number of runs of each test, and it "
               "does not report their performance.  Additionally, it runs "
               "all of the unit tests in the executable (i.e. those that "
               "use the unittest library).\n"
               "\n"
               "\"Perf test mode\" runs many iterations of each perf test, "
               "and reports the performance results.  It does not run any "
               "unittest test cases.\n"
               "\n"
               "Options:\n"
               "  --out FILENAME\n"
               "      Filename to write JSON results data to.  If this is "
               "omitted, no JSON output is produced.\n"
               "  --filter REGEX\n"
               "      Regular expression that specifies a subset of tests "
               "to run.  By default, all the tests are run.\n"
               "  --runs NUMBER\n"
               "      Number of times to run each test.\n",
               argv[0], argv[0]);
        return 1;
    }

    bool success = true;

    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        // Drop the "-p" argument.  Keep argv[0] because getopt_long()
        // prints it in error messages.
        argv[1] = argv[0];
        argc--;
        argv++;
        if (!PerfTestMode(argc, argv)) {
            success = false;
        }
    } else {
        printf("Running perf tests in unit test mode...\n");
        {
            // Run each test a small number of times to ensure that doing
            // multiple runs works OK.
            const int kRunCount = 3;
            ResultsSet unused_results;
            if (!RunTests(g_tests, kRunCount, "", stdout, &unused_results)) {
                success = false;
            }
        }

        // In unit test mode, we pass all command line arguments on to the
        // unittest library.
        printf("Running unit tests...\n");
        if (!unittest_run_all_tests(argc, argv)) {
            success = false;
        }
    }

    return success ? 0 : 1;
}

}  // namespace perftest

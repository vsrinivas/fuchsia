// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <perftest/runner.h>

#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <regex.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/vector.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-engine/context.h>
#include <trace-engine/instrumentation.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>
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
        : run_count_(run_count) {}

    void SetBytesProcessedPerRun(uint64_t bytes) override {
        if (started_) {
            SetError("SetBytesProcessedPerRun() was called after KeepRunning()");
            return;
        }
        if (bytes == 0) {
            SetError("Zero argument to SetBytesProcessedPerRun()");
            return;
        }
        if (bytes_processed_per_run_ != 0) {
            SetError("Multiple calls to SetBytesProcessedPerRun()");
            return;
        }
        bytes_processed_per_run_ = bytes;
    }

    void DeclareStep(const char* name) override {
        if (started_) {
            SetError("DeclareStep() was called after KeepRunning()");
            return;
        }
        step_names_.push_back(name);
    }

    void NextStep() override {
        if (unlikely(next_idx_ >= end_of_run_idx_)) {
            SetError("Too many calls to NextStep()");
            return;
        }
        timestamps_[next_idx_] = zx_ticks_get();
        ++next_idx_;
    }

    bool KeepRunning() override {
        uint64_t timestamp = zx_ticks_get();
        if (unlikely(next_idx_ != end_of_run_idx_)) {
            // Slow path, including error cases.
            if (error_) {
                return false;
            }
            if (started_) {
                SetError("Wrong number of calls to NextStep()");
                return false;
            }
            // First call to KeepRunning().
            step_count_ = static_cast<uint32_t>(step_names_.size());
            if (step_count_ == 0) {
                step_count_ = 1;
            }
            // Add 1 because we store timestamps for the start of each test
            // run (which serve as timestamps for the end of the previous
            // test run), plus one more timestamp for the end of the last
            // test run.
            timestamps_size_ = run_count_ * step_count_ + 1;
            timestamps_.reset(new uint64_t[timestamps_size_]);
            // Clear the array in order to fault in the pages.  This should
            // prevent page faults occurring as we cross page boundaries
            // when writing a test's running times (which would affect the
            // first test case but not later test cases).
            memset(timestamps_.get(), 0,
                   sizeof(timestamps_[0]) * timestamps_size_);
            next_idx_ = 1;
            end_of_run_idx_ = step_count_;
            started_ = true;
            timestamps_[0] = zx_ticks_get();
            return run_count_ != 0;
        }
        if (unlikely(next_idx_ == timestamps_size_ - 1)) {
            // End reached.
            if (finished_) {
                SetError("Too many calls to KeepRunning()");
                return false;
            }
            timestamps_[next_idx_] = timestamp;
            finished_ = true;
            return false;
        }
        timestamps_[next_idx_] = timestamp;
        ++next_idx_;
        end_of_run_idx_ += step_count_;
        return true;
    }

    // Returns nullptr on success, or an error string on failure.
    const char* RunTestFunc(const char* test_name,
                            const fbl::Function<TestFunc>& test_func) {
        TRACE_DURATION("perftest", "test_group", "test_name", test_name);
        overall_start_time_ = zx_ticks_get();
        bool result = test_func(this);
        overall_end_time_ = zx_ticks_get();
        if (error_) {
            return error_;
        }
        if (!finished_) {
            return "Too few calls to KeepRunning()";
        }
        if (!result) {
            return "Test function returned false";
        }
        return nullptr;
    }

    void CopyTimeResults(const char* test_suite, const char* test_name,
                         ResultsSet* dest) const {
        // bytes_processed_per_run is used for calculating throughput, but
        // throughput is only really meaningful to calculate for the test
        // overall, not for individual steps.  Therefore we only report
        // bytes_processed_per_run on the overall times.

        // Report the times for each test run.
        if (step_count_ == 1 || bytes_processed_per_run_ != 0) {
            TestCaseResults* results = dest->AddTestCase(
                test_suite, test_name, "nanoseconds");
            results->bytes_processed_per_run = bytes_processed_per_run_;
            CopyStepTimes(0, step_count_, results);
        }

        if (step_count_ > 1) {
            // Report times for individual steps.
            for (uint32_t step = 0; step < step_count_; ++step) {
                fbl::String name = fbl::StringPrintf(
                    "%s.%s", test_name, step_names_[step].c_str());
                TestCaseResults* results = dest->AddTestCase(
                    test_suite, name, "nanoseconds");
                CopyStepTimes(step, step + 1, results);
            }
        }
    }

    // Output a trace event for each of the test runs.  Since we do this
    // after the test runs took place (using the timestamps we recorded),
    // we avoid incurring the overhead of the tracing system on each test
    // run.
    void WriteTraceEvents() {
        trace_string_ref_t category_ref;
        trace_context_t* context =
            trace_acquire_context_for_category("perftest", &category_ref);
        if (!context) {
            return;
        }
        trace_thread_ref_t thread_ref;
        trace_context_register_current_thread(context, &thread_ref);

        auto WriteEvent = [&](trace_string_ref_t* name_ref,
                              uint64_t start_time, uint64_t end_time) {
            trace_context_write_duration_begin_event_record(
                context, start_time,
                &thread_ref, &category_ref, name_ref, nullptr, 0);
            trace_context_write_duration_end_event_record(
                context, end_time,
                &thread_ref, &category_ref, name_ref, nullptr, 0);
        };

        trace_string_ref_t test_setup_string;
        trace_string_ref_t test_run_string;
        trace_string_ref_t test_step_string;
        trace_string_ref_t test_teardown_string;
        trace_context_register_string_literal(
            context, "test_setup", &test_setup_string);
        trace_context_register_string_literal(
            context, "test_run", &test_run_string);
        trace_context_register_string_literal(
            context, "test_step", &test_step_string);
        trace_context_register_string_literal(
            context, "test_teardown", &test_teardown_string);

        WriteEvent(&test_setup_string, overall_start_time_, timestamps_[0]);
        for (uint32_t run = 0; run < run_count_; ++run) {
            WriteEvent(&test_run_string,
                       GetTimestamp(run, 0),
                       GetTimestamp(run + 1, 0));
            if (step_count_ > 1) {
                for (uint32_t step = 0; step < step_count_; ++step) {
                    WriteEvent(&test_step_string,
                               GetTimestamp(run, step),
                               GetTimestamp(run, step + 1));
                }
            }
        }
        WriteEvent(&test_teardown_string,
                   timestamps_[timestamps_size_ - 1], overall_end_time_);
    }

private:
    void SetError(const char* str) {
        if (!error_) {
            error_ = str;
        }
    }

    // The start and end times of run R are GetTimestamp(R, 0) and
    // GetTimestamp(R+1, 0).
    // The start and end times of step S within run R are GetTimestamp(R,
    // S) and GetTimestamp(R, S+1).
    uint64_t GetTimestamp(uint32_t run_number, uint32_t step_number) const {
        uint32_t index = run_number * step_count_ + step_number;
        ZX_ASSERT(step_number <= step_count_);
        ZX_ASSERT(index < timestamps_size_);
        return timestamps_[index];
    }

    void CopyStepTimes(uint32_t start_step_index, uint32_t end_step_index,
                       TestCaseResults* results) const {
        double nanoseconds_per_tick =
            1e9 / static_cast<double>(zx_ticks_per_second());

        // Copy the timing results, converting timestamps to elapsed times.
        results->values.reserve(run_count_);
        for (uint32_t run = 0; run < run_count_; ++run) {
            uint64_t time_taken = (GetTimestamp(run, end_step_index) -
                                   GetTimestamp(run, start_step_index));
            results->AppendValue(
                static_cast<double>(time_taken) * nanoseconds_per_tick);
        }
    }

    // Number of test runs that we intend to do.
    uint32_t run_count_;
    // Number of steps per test run.  Once initialized, this is >= 1.
    uint32_t step_count_;
    // Names for steps.  May be empty if the test has only one step.
    fbl::Vector<fbl::String> step_names_;
    // error_ is set to non-null if an error occurs.
    const char* error_ = nullptr;
    // Array of timestamps for the starts and ends of test runs and of
    // steps within runs.  GetTimestamp() describes the array layout.
    fbl::unique_ptr<uint64_t[]> timestamps_;
    // Number of elements allocated for timestamps_ array.
    uint32_t timestamps_size_ = 0;
    // Whether the first KeepRunning() call has occurred.
    bool started_ = false;
    // Whether the last KeepRunning() call has occurred.
    bool finished_ = false;
    // Next index in timestamps_ for writing a timestamp to.  The initial
    // value helps catch invalid NextStep() calls.
    uint32_t next_idx_ = ~static_cast<uint32_t>(0);
    // Index in timestamp_ for writing the end of the current run.
    uint32_t end_of_run_idx_ = 0;
    // Start time, before the test's setup phase.
    uint64_t overall_start_time_;
    // End time, after the test's teardown phase.
    uint64_t overall_end_time_;
    // Used for calculating throughput in bytes per unit time.
    uint64_t bytes_processed_per_run_ = 0;
};

} // namespace

void RegisterTest(const char* name, fbl::Function<TestFunc> test_func) {
    if (!g_tests) {
        g_tests = new internal::TestList;
    }
    internal::NamedTest new_test{name, fbl::move(test_func)};
    g_tests->push_back(fbl::move(new_test));
}

bool RunTest(const char* test_suite, const char* test_name,
             const fbl::Function<TestFunc>& test_func,
             uint32_t run_count, ResultsSet* results_set,
             fbl::String* error_out) {
    RepeatStateImpl state(run_count);
    const char* error = state.RunTestFunc(test_name, test_func);
    if (error) {
        if (error_out) {
            *error_out = error;
        }
        return false;
    }

    state.CopyTimeResults(test_suite, test_name, results_set);
    state.WriteTraceEvents();
    return true;
}

namespace internal {

bool RunTests(const char* test_suite, TestList* test_list, uint32_t run_count,
              const char* regex_string, FILE* log_stream,
              ResultsSet* results_set) {
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

        fbl::String error_string;
        if (!RunTest(test_suite, test_name, test_case.test_func, run_count,
                     results_set, &error_string)) {
            fprintf(log_stream, "Error: %s\n", error_string.c_str());
            fprintf(log_stream, "[  FAILED  ] %s\n", test_name);
            ok = false;
            continue;
        }
        fprintf(log_stream, "[       OK ] %s\n", test_name);
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
        {"enable-tracing", no_argument, nullptr, 't'},
        {"startup-delay", required_argument, nullptr, 'd'},
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
            // Convert string to number (uint32_t).
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
        case 't':
            dest->enable_tracing = true;
            break;
        case 'd': {
            // Convert string to number (double type).
            char* end;
            double val = strtod(optarg, &end);
            if (*end != '\0' || *optarg == '\0') {
                fprintf(stderr,
                        "Invalid argument for --startup-delay: \"%s\"\n",
                        optarg);
                exit(1);
            }
            dest->startup_delay_seconds = val;
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

} // namespace internal

static void* TraceProviderThread(void* thread_arg) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider provider(loop.dispatcher());
    loop.Run();
    return nullptr;
}

static void StartTraceProvider() {
    pthread_t tid;
    int err = pthread_create(&tid, nullptr, TraceProviderThread, nullptr);
    ZX_ASSERT(err == 0);
    err = pthread_detach(tid);
    ZX_ASSERT(err == 0);
}

static bool PerfTestMode(const char* test_suite, int argc, char** argv) {
    internal::CommandArgs args;
    internal::ParseCommandArgs(argc, argv, &args);

    if (args.enable_tracing) {
        StartTraceProvider();
    }
    zx_duration_t duration =
        static_cast<zx_duration_t>(ZX_SEC(1) * args.startup_delay_seconds);
    zx_nanosleep(zx_deadline_after(duration));

    ResultsSet results;
    bool success = RunTests(test_suite, g_tests, args.run_count,
                            args.filter_regex, stdout, &results);

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

int PerfTestMain(int argc, char** argv, const char* test_suite) {
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
               "omitted, no JSON output is produced. JSON output will conform to this schema: "
               "//zircon/system/ulib/perftest/performance-results-schema.json\n"
               "  --filter REGEX\n"
               "      Regular expression that specifies a subset of tests "
               "to run.  By default, all the tests are run.\n"
               "  --runs NUMBER\n"
               "      Number of times to run each test.\n"
               "  --enable-tracing\n"
               "      Enable use of Fuchsia tracing: Enable registering as a "
               "TraceProvider.  This is off by default because the "
               "TraceProvider gets registered asynchronously on a background "
               "thread (see TO-650), and that activity could introduce noise "
               "to the tests.\n"
               "  --startup-delay SECONDS\n"
               "      Delay in seconds to wait on startup, after registering "
               "a TraceProvider.  This allows working around a race condition "
               "where tracing misses initial events from newly-registered "
               "TraceProviders (see TO-650).\n",
               argv[0], argv[0]);
        return 1;
    }

    bool success = true;

    // Check whether to run in perf test mode.
    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        // Drop the "-p" argument.  Keep argv[0] because getopt_long()
        // prints it in error messages.
        argv[1] = argv[0];
        argc--;
        argv++;
        if (!PerfTestMode(test_suite, argc, argv)) {
            success = false;
        }
    } else {
        printf("Running perf tests in unit test mode...\n");
        {
            // Run each test a small number of times to ensure that doing
            // multiple runs works OK.
            const int kRunCount = 3;
            ResultsSet unused_results;
            if (!RunTests(test_suite, g_tests, kRunCount, "", stdout, &unused_results)) {
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

} // namespace perftest

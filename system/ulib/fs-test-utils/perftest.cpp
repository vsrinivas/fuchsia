// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-test-utils/perftest.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

namespace fs_test_utils {
namespace {

// Gather some info about the executed tests, and use it to display
// gTest lookalike summary.
struct TestStats {
    uint32_t passed = 0;
    uint32_t failed = 0;
    uint32_t skipped = 0;
    uint32_t total = 0;
};

uint64_t GetDelta(zx::ticks start) {
    return (fzl::TicksToNs(zx::ticks::now() - start).to_msecs());
}

void PrintTestStart(const fbl::String& name, FILE* out) {
    if (out) {
        fprintf(out, "[ RUN      ] %s\n", name.c_str());
    }
}

void PrintTestSkipped(const fbl::String& name, zx::ticks start, FILE* out) {
    if (out) {
        fprintf(out, "[  SKIPPED ] %s(%lu ms total)\n", name.c_str(), GetDelta(start));
    }
}

void PrintTestFailed(const fbl::String& name, zx::ticks start, FILE* out) {
    if (out) {
        fprintf(out, "[   FAILED ] %s(%lu ms total)\n", name.c_str(), GetDelta(start));
    }
}

void PrintTestPassed(const fbl::String& name, zx::ticks start, FILE* out) {
    if (out) {
        fprintf(out, "[   PASSED ] %s(%lu ms total)\n", name.c_str(), GetDelta(start));
    }
}

void PrintTestCaseStart(const fbl::String& name, size_t test_count, FILE* out) {
    if (out) {
        fprintf(out, "[----------] %lu tests from %s\n", test_count, name.c_str());
    }
}

void PrintTestCaseEnd(const fbl::String& name, size_t test_count, zx::ticks start, FILE* out) {
    if (out) {
        fprintf(out, "[----------] %lu tests from %s(%lu ms total)\n\n", test_count, name.c_str(),
                GetDelta(start));
    }
}

void PrintTestsCasesSummary(size_t test_case_count, const TestStats& stats, zx::ticks start,
                            FILE* out) {
    if (out) {
        fprintf(out, "[==========] %d tests from %lu test cases ran. (%lu ms total)\n", stats.total,
                test_case_count, fzl::TicksToNs(zx::ticks::now() - start).to_msecs());
        fprintf(out, "[  PASSED  ] %d tests.\n", stats.passed);
        fprintf(out, "[  FAILED  ] %d tests.\n", stats.failed);
        fprintf(out, "[  SKIPPED ] %d tests.\n", stats.skipped);
    }
}

void PrintUsage(char* arg0, FILE* out) {
    fprintf(out, R"(
Usage:

    %s [mode] [fixture options] [test options]    
    Runs a set of benchmarks and write results.

    Note: Argument order matters, latest overrides earliest.

    [Mode]
        -h,--help                      Print usage description. This message.

        -p                             Performance test mode. Default mode is Unit test.

    [Fixture Options]
        --block_device PATH            The block device exposed in PATH will be used as block 
                                       device.

        --use_ramdisk                  A ramdisk will be used as block device.

        --ramdisk_block_size SIZE      Size in bytes of the ramdisk's block.

        --ramdisk_block_count COUNT    Number of blocks in the ramdisk.

        --use_fvm                      A FVM will be created on the block device.

        --fvm_slice_size SIZE          Size in bytes of the FVM's slices.

        --fs FS_NAME                   Will use FS_NAME filesystem to format the block device. 
                                       (Options: blobfs, minfs.

    [Test Options]
         --out PATH                    In performace test mode, collected results will be written to 
                                       PATH.

         --summary_path PATH           In performace test mode, result summary statistics will be 
                                       written to PATH.

         --print_statistics            In performace test mode, result summary statistics will be
                                       written to STDOUT.
    
         --run COUNT                   In performace test mode, limits the number of times to execute 
                                       each test to COUNT.

)",
            arg0);
    return;
}

bool HasEnoughSpace(const fbl::String& block_device_path, size_t required_space) {
    if (required_space == 0) {
        return true;
    }

    fbl::unique_fd fd(open(block_device_path.c_str(), O_RDONLY));
    block_info_t block_device_info;
    ssize_t result = ioctl_block_get_info(fd.get(), &block_device_info);
    zx_status_t status = (result > 0) ? ZX_OK : static_cast<zx_status_t>(result);
    if (status != ZX_OK) {
        LOG_ERROR(status, "Failed to verify block_device size.\n %s\n", block_device_path.c_str());
        return false;
    }

    if (required_space > block_device_info.block_count * block_device_info.block_size) {
        return false;
    }
    return true;
}

void RunTest(const fbl::String& test_case_name, const TestInfo& test, uint32_t sample_count,
             bool skip, Fixture* fixture, perftest::ResultsSet* result_set, TestStats* stats,
             FILE* out) {
    zx::ticks test_start = zx::ticks::now();
    PrintTestStart(test.name, out);
    stats->total++;
    fbl::String error;
    if (skip) {
        stats->skipped++;
        PrintTestSkipped(test.name, test_start, out);
        return;
    }
    auto test_wrapper = [&test, fixture](perftest::RepeatState* state) {
        bool result = test.test_fn(state, fixture);
        return result;
    };

    bool failed = !perftest::RunTest(test_case_name.c_str(), test.name.c_str(), test_wrapper,
                                     sample_count, result_set, &error);
    if (failed) {
        // Log if the error is from the perftest lib.
        if (!error.empty()) {
            LOG_ERROR(ZX_ERR_INTERNAL, "%s\n", error.c_str());
        }
        stats->failed++;
        PrintTestFailed(test.name, test_start, out);
        return;
    }
    PrintTestPassed(test.name, test_start, out);
    stats->passed++;
    return;
}

// Runs all tests in the given testcase.
void RunTestCase(const FixtureOptions& fixture_options,
                 const PerformanceTestOptions& performance_test_options,
                 const TestCaseInfo& test_case, perftest::ResultsSet* result_set,
                 TestStats* global_stats, FILE* out) {
    Fixture fixture(fixture_options);

    zx::ticks start = zx::ticks::now();
    PrintTestCaseStart(test_case.name, test_case.tests.size(), out);
    bool skip_tests = (fixture.SetUpTestCase() != ZX_OK);
    bool setUp = true;
    for (auto& test : test_case.tests) {
        // Verify that the disk has enough space to run the test. This is set up by the user
        // since the actual may change depending on the test input.
        skip_tests = !HasEnoughSpace(fixture.GetFsBlockDevice(), test.required_disk_space);
        if (skip_tests) {
            LOG_ERROR(ZX_ERR_NO_SPACE, "Not enough space on disk to run test.\n");
        } else if (setUp) {
            skip_tests = (fixture.SetUp() != ZX_OK);
            setUp = false;
        }
        uint32_t actual_sample_count = test_case.sample_count;
        if (actual_sample_count == 0 || performance_test_options.is_unittest) {
            actual_sample_count = performance_test_options.sample_count;
        }
        RunTest(test_case.name, test, actual_sample_count, skip_tests, &fixture, result_set,
                global_stats, out);
        if (test_case.teardown) {
            fixture.TearDown();
            setUp = true;
        }
    }
    if (!test_case.teardown) {
        fixture.TearDown();
    }
    fixture.TearDownTestCase();
    PrintTestCaseEnd(test_case.name, test_case.tests.size(), start, out);
    return;
}

} // namespace

bool PerformanceTestOptions::IsValid(fbl::String* error) const {
    if (is_unittest) {
        return true;
    }

    if (result_path.empty()) {
        *error = "result_path must be set.\n";
        return false;
    }

    if (result_path == summary_path) {
        *error = "result_path and summary_path cannot point to the same file.\n";
        return false;
    }

    if (sample_count == 0) {
        *error = "sample_count must be a positive integer.\n";
        return false;
    }

    return true;
}

bool RunTestCases(const FixtureOptions& fixture_options,
                  const PerformanceTestOptions& performance_test_options,
                  const fbl::Vector<TestCaseInfo>& test_cases, FILE* out) {
    TestStats stats;
    perftest::ResultsSet result_set;
    bool error = false;
    zx::ticks start = zx::ticks::now();
    for (auto& test_case : test_cases) {
        RunTestCase(fixture_options, performance_test_options, test_case, &result_set, &stats, out);
    }
    PrintTestsCasesSummary(test_cases.size(), stats, start, out);
    if (performance_test_options.print_statistics) {
        fprintf(out, "\n");
        result_set.PrintSummaryStatistics(out);
        fprintf(out, "\n");
    }

    if (!performance_test_options.summary_path.empty()) {
        FILE* fp = fopen(performance_test_options.summary_path.c_str(), "w");
        if (fp) {
            result_set.PrintSummaryStatistics(fp);
            fclose(fp);
        } else {
            LOG_ERROR(ZX_ERR_IO, "%s\n", strerror(errno));
            error = true;
        }
    }

    if (!performance_test_options.result_path.empty()) {
        FILE* fp = fopen(performance_test_options.result_path.c_str(), "w");
        if (fp) {
            result_set.WriteJSON(fp);
            fclose(fp);
        } else {
            LOG_ERROR(ZX_ERR_IO, "%s\n", strerror(errno));
        }
    }
    return stats.failed == 0 && !error;
}

bool ParseCommandLineArgs(int argc, const char* const* argv, FixtureOptions* fixture_options,
                          PerformanceTestOptions* performance_test_options, FILE* out) {
    static const struct option opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"block_device", required_argument, nullptr, 0},
        {"use_ramdisk", no_argument, nullptr, 0},
        {"ramdisk_block_size", required_argument, nullptr, 0},
        {"ramdisk_block_count", required_argument, nullptr, 0},
        {"use_fvm", no_argument, nullptr, 0},
        {"fvm_slice_size", required_argument, nullptr, 0},
        {"fs", required_argument, nullptr, 0},
        {"out", required_argument, nullptr, 0},
        {"summary_path", required_argument, nullptr, 0},
        {"print_statistics", no_argument, nullptr, 0},
        {"runs", required_argument, nullptr, 0},
        {0, 0, 0, 0},
    };
    // Resets the internal state of getopt*, making this function idempotent.
    optind = 0;
    bool ramdisk_set = false;
    bool block_device_set = false;

    *performance_test_options = PerformanceTestOptions::UnitTest();
    // get_opt expects non const pointers.
    char** argvs = const_cast<char**>(argv);

    int c = -1;
    int option_index = -1;
    while ((c = getopt_long(argc, argvs, "ph", opts, &option_index)) >= 0) {
        switch (c) {
        case 0:
            switch (option_index) {
            case 0:
                PrintUsage(argvs[0], out);
                return false;
            case 1:
                fixture_options->block_device_path = optarg;
                block_device_set = true;
                break;
            case 2:
                fixture_options->use_ramdisk = true;
                ramdisk_set = true;
                break;
            case 3:
                fixture_options->ramdisk_block_size = atoi(optarg);
                break;
            case 4:
                fixture_options->ramdisk_block_count = atoi(optarg);
                break;
            case 5:
                fixture_options->use_fvm = true;
                break;
            case 6:
                fixture_options->fvm_slice_size = atoi(optarg);
                break;
            case 7:
                if (strcmp(optarg, "minfs") == 0) {
                    fixture_options->fs_type = DISK_FORMAT_MINFS;
                } else if (strcmp(optarg, "blobfs") == 0) {
                    fixture_options->fs_type = DISK_FORMAT_BLOBFS;
                } else {
                    LOG_ERROR(ZX_ERR_INVALID_ARGS,
                              "Unknown disk_format %s. Support values are minfs and blobfs.\n",
                              optarg);
                }
                break;
            case 8:
                performance_test_options->result_path = optarg;
                break;
            case 9:
                performance_test_options->summary_path = optarg;
                break;
            case 10:
                performance_test_options->print_statistics = true;
                break;
            case 11:
                performance_test_options->sample_count = atoi(optarg);
                break;
            default:
                break;
            }
            break;
        case 'p':
            *performance_test_options = PerformanceTestOptions::PerformanceTest();
            break;
        case 'h':
        default:
            PrintUsage(argvs[0], out);
            return false;
        }
    }

    // Unset ramdisk when not requested and block device is passed.
    if (block_device_set && !ramdisk_set) {
        fixture_options->use_ramdisk = false;
    }

    bool ok = true;
    fbl::String error;
    if (!fixture_options->IsValid(&error)) {
        LOG_ERROR(ZX_ERR_INVALID_ARGS, "%s\n", error.c_str());
        ok = false;
    }

    if (!performance_test_options->IsValid(&error)) {
        LOG_ERROR(ZX_ERR_INVALID_ARGS, "%s\n", error.c_str());
        ok = false;
    }

    if (!ok) {
        PrintUsage(argvs[0], out);
    }

    return ok;
}

} // namespace fs_test_utils

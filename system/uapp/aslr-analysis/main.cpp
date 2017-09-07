// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <math.h>
#include <mxio/io.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

struct ReportInfo {
    uintptr_t exec_addr = 0;
    uintptr_t first_stack = 0;
    uintptr_t first_heap_alloc = 0;
    uintptr_t libc = 0;
    uintptr_t vdso = 0;
};

namespace {

static const char* kBinName = "/boot/bin/aslr-analysis";

int GatherReports(const char* test_bin, fbl::Array<ReportInfo>* reports);
unsigned int AnalyzeField(const fbl::Array<ReportInfo>& reports,
                          uintptr_t ReportInfo::*field);
double ApproxBinomialCdf(double p, double N, double n);
int TestRunMain(int argc, char** argv);
mx_status_t LaunchTestRun(const char* bin, mx_handle_t h, mx_handle_t* out);
int JoinProcess(mx_handle_t proc);
} // namespace

int main(int argc, char** argv) {
    // TODO(teisenbe): This is likely too low; compute how many runs we
    // will need for statistical confidence
    static const int kNumRuns = 1000;

    if (argc > 1 && !strcmp(argv[1], "testrun")) {
        return TestRunMain(argc, argv);
    }

    struct stat stat_info;
    if (stat(kBinName, &stat_info) != 0 || !S_ISREG(stat_info.st_mode)) {
        printf("Could not find %s for running tests\n", kBinName);
        return 1;
    }

    fbl::Array<ReportInfo> reports(new ReportInfo[kNumRuns], kNumRuns);
    if (!reports) {
        printf("Failed to allocate reports\n");
        return 1;
    }

    int ret = GatherReports(kBinName, &reports);
    if (ret != 0) {
        return 1;
    }
    printf("Finished gathering reports\n");

    unsigned int bits;
    bits = AnalyzeField(reports, &ReportInfo::exec_addr);
    printf("exec_addr: %d bits\n", bits);
    bits = AnalyzeField(reports, &ReportInfo::first_stack);
    printf("first_stack: %d bits\n", bits);
    bits = AnalyzeField(reports, &ReportInfo::first_heap_alloc);
    printf("first_heap_alloc: %d bits\n", bits);
    bits = AnalyzeField(reports, &ReportInfo::libc);
    printf("libc: %d bits\n", bits);
    bits = AnalyzeField(reports, &ReportInfo::vdso);
    printf("vdso: %d bits\n", bits);

    return 0;
}

namespace {

// Computes P(X <= n), approximated via the normal distribution
double ApproxBinomialCdf(double p, double N, double n) {
    // https://en.wikipedia.org/wiki/Normal_distribution#Cumulative_distribution_function
    // https://en.wikipedia.org/wiki/Binomial_distribution#Normal_approximation
    const double mu = N * .5;
    const double sigma = sqrt(N * .5 * .5);
    // Note we add 1/2 to n below as a continuity correction.
    return 0.5 * (1. + erf((n + .5 - mu) / (sigma * sqrt(2.))));
}

// Perform an approximate two-sided binomial test across each bit-position for
// all of the reports.
//
// |reports| is an array of samples gathered from launching processes
//
// |field| is a pointer to the field that is being analyzed
//
// TODO: Investigate if there are better approaches than the two-sided binomial
// test.
// TODO: Do further analysis to account for potential non-independence of bits
unsigned int AnalyzeField(const fbl::Array<ReportInfo>& reports,
                          uintptr_t ReportInfo::*field) {
    int good_bits = 0;

    const size_t count = reports.size();
    for (unsigned int bit = 0; bit < sizeof(uintptr_t) * 8; ++bit) {
        size_t ones = 0;
        for (unsigned int i = 0; i < count; ++i) {
            uintptr_t val = reports[i].*field;
            if (val & (1ULL << bit)) {
                ones++;
            }
        }

        size_t n = ones;
        // Since we're doing a two-tailed test, set n to be the left tail
        // bound to simplify the calculation.
        if (n > count / 2) {
            n = count - n;
        }

        // Probability that we'd see at most ones 1s or at least count/2 +
        // (count/2 - ones) 1s (i.e., the two-sided probability).  Since p=.5,
        // these two propabilities are the same.
        //
        // Note the normal approximation is valid for us, since we are dealing with
        // p=0.5 and N > 9(1 - p)/p and N > 9p/(1-p) (a common rule of thumb).
        const double p = 2 * ApproxBinomialCdf(0.5, static_cast<double>(count),
                                               static_cast<double>(n));

        // Test the result against our alpha-value.  If p <= alpha, then the
        // alternate hypothesis of a biased bit is considered true.  We choose
        // alpha = 0.10, rather than the more conventional 0.05, to bias
        // ourselves more towards false positives (considering a bit to
        // be biased) rather than more false negatives.
        if (p > 0.10) {
            good_bits++;
        }
    }
    return good_bits;
}

int GatherReports(const char* test_bin, fbl::Array<ReportInfo>* reports) {
    const size_t count = reports->size();
    for (unsigned int run = 0; run < count; ++run) {
        mx_handle_t handles[2];
        mx_status_t status = mx_channel_create(0, &handles[0], &handles[1]);
        if (status != MX_OK) {
            printf("Failed to create channel for test run\n");
            return -1;
        }

        mx_handle_t proc;
        if ((status = LaunchTestRun(test_bin, handles[1], &proc)) != MX_OK) {
            mx_handle_close(handles[0]);
            printf("Failed to launch testrun: %d\n", status);
            return -1;
        }

        int ret = JoinProcess(proc);
        mx_handle_close(proc);

        if (ret != 0) {
            mx_handle_close(handles[0]);
            printf("Failed to join testrun: %d\n", ret);
            return -1;
        }

        ReportInfo* report = &(*reports)[run];

        uint32_t len = sizeof(*report);
        status = mx_channel_read(handles[0], 0, report, NULL, len, 0, &len, NULL);
        if (status != 0 || len != sizeof(*report)) {
            printf("Failed to read report: status %d, len %u\n", status, len);
            mx_handle_close(handles[0]);
            return -1;
        }

        mx_handle_close(handles[0]);
    }
    return 0;
}

int TestRunMain(int argc, char** argv) {
    mx_handle_t report_pipe =
        mx_get_startup_handle(PA_HND(PA_USER1, 0));

    ReportInfo report;

    // TODO(teisenbe): Ideally we should get measurements closer to the source
    // of the mapping rather than inferring from data locations
    report.exec_addr = (uintptr_t)&main;
    report.first_stack = (uintptr_t)&report_pipe;
    report.first_heap_alloc = (uintptr_t)malloc(1);
    report.libc = (uintptr_t)&memcpy;
    report.vdso = (uintptr_t)&mx_channel_write;

    mx_status_t status =
        mx_channel_write(report_pipe, 0, &report, sizeof(report), NULL, 0);
    if (status != MX_OK) {
        return status;
    }

    return 0;
}

// This function unconditionally consumes the handle h.
mx_status_t LaunchTestRun(const char* bin, mx_handle_t h, mx_handle_t* out) {
    launchpad_t* lp;
    mx_handle_t proc;
    mx_handle_t hnd[1];
    mx_handle_t job;
    uint32_t ids[1];
    const char* args[] = {bin, "testrun"};
    const char* errmsg;

    mx_status_t status = mx_handle_duplicate(mx_job_default(), MX_RIGHT_SAME_RIGHTS, &job);
    if (status != MX_OK) {
        return status;
    }

    ids[0] = PA_USER1;
    hnd[0] = h;
    launchpad_create(job, "testrun", &lp);
    launchpad_load_from_file(lp, bin);
    launchpad_set_args(lp, fbl::count_of(args), args);
    launchpad_add_handles(lp, fbl::count_of(hnd), hnd, ids);

    status = launchpad_go(lp, &proc, &errmsg);
    if (status != MX_OK) {
        printf("launch failed (%d): %s\n", status, errmsg);
        return status;
    }

    *out = proc;
    return MX_OK;
}

int JoinProcess(mx_handle_t proc) {
    mx_status_t status =
        mx_object_wait_one(proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);
    if (status != MX_OK) {
        printf("join failed? %d\n", status);
        return -1;
    }

    // read the return code
    mx_info_process_t proc_info;
    if ((status = mx_object_get_info(proc, MX_INFO_PROCESS, &proc_info,
                                     sizeof(proc_info), NULL, NULL)) < 0) {
        printf("handle_get_info failed? %d\n", status);
        return -1;
    }

    return proc_info.return_code;
}

} // namespace

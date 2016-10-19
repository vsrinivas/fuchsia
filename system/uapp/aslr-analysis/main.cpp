// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <limits.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <math.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <mxtl/array.h>
#include <mxtl/auto_call.h>
#include <mxtl/unique_ptr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

struct ReportInfo {
    uintptr_t exec_addr = 0;
    uintptr_t first_stack = 0;
    uintptr_t first_heap_alloc = 0;
    uintptr_t libc = 0;
    uintptr_t vdso = 0;
};

namespace {

int GatherReports(char* test_bin, mxtl::Array<ReportInfo>* reports);
unsigned int AnalyzeField(const mxtl::Array<ReportInfo>& reports,
                          uintptr_t ReportInfo::*field);
double ApproxBinomialCdf(double p, double N, double n);
int TestRunMain(int argc, char** argv);
mx_handle_t LaunchTestRun(char* bin, mx_handle_t h);
int JoinProcess(mx_handle_t proc);
} // namespace

int main(int argc, char** argv) {
    // TODO(teisenbe): This is likely too low; compute how many runs we
    // will need for statistical confidence
    static const int kNumRuns = 1000;

    if (argc > 1 && !strcmp(argv[1], "testrun")) {
        return TestRunMain(argc, argv);
    }

    mxtl::Array<ReportInfo> reports(new ReportInfo[kNumRuns], kNumRuns);
    if (!reports) {
        printf("Failed to allocate reports\n");
        return 1;
    }

    int ret = GatherReports(argv[0], &reports);
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
unsigned int AnalyzeField(const mxtl::Array<ReportInfo>& reports,
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

int GatherReports(char* test_bin, mxtl::Array<ReportInfo>* reports) {
    const size_t count = reports->size();
    for (unsigned int run = 0; run < count; ++run) {
        mx_handle_t handles[2];
        mx_status_t status = mx_msgpipe_create(handles, 0);
        if (status != NO_ERROR) {
            printf("Failed to create message pipe for test run\n");
            return -1;
        }

        mx_handle_t proc = LaunchTestRun(test_bin, handles[1]);
        if (proc < 0) {
            mx_handle_close(handles[0]);
            mx_handle_close(handles[1]);
            printf("Failed to launch testrun\n");
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
        status = mx_msgpipe_read(handles[0], report, &len, NULL, 0, 0);
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
        mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER1, 0));

    ReportInfo report;

    // TODO(teisenbe): Ideally we should get measurements closer to the source
    // of the mapping rather than inferring from data locations
    report.exec_addr = (uintptr_t)&main;
    report.first_stack = (uintptr_t)&report_pipe;
    report.first_heap_alloc = (uintptr_t)malloc(1);
    report.libc = (uintptr_t)&memcpy;
    report.vdso = (uintptr_t)&mx_msgpipe_write;

    mx_status_t status =
        mx_msgpipe_write(report_pipe, &report, sizeof(report), NULL, 0, 0);
    if (status != NO_ERROR) {
        return status;
    }

    return 0;
}

mx_handle_t LaunchTestRun(char* bin, mx_handle_t h) {
    mx_handle_t hnd[1];
    uint32_t ids[1];
    ids[0] = MX_HND_TYPE_USER1;
    hnd[0] = h;
    const char* args[] = {bin, "testrun"};
    return launchpad_launch(bin, countof(args), args, NULL, countof(hnd), hnd,
                            ids);
}

int JoinProcess(mx_handle_t proc) {
    mx_signals_state_t state;
    mx_status_t status =
        mx_handle_wait_one(proc, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, &state);
    if (status != NO_ERROR) {
        printf("join failed? %d\n", status);
        return -1;
    }

    // read the return code
    mx_info_process_t proc_info;
    mx_ssize_t ret =
        mx_object_get_info(proc, MX_INFO_PROCESS, sizeof(proc_info.rec),
                           &proc_info, sizeof(proc_info));
    if (ret != sizeof(proc_info)) {
        printf("handle_get_info failed? %zd\n", ret);
        return -1;
    }

    return proc_info.rec.return_code;
}

} // namespace

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libgen.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <runtests-utils/posix-run-test.h>
#include <runtests-utils/runtests-utils.h>

namespace {

class PosixStopwatch final : public runtests::Stopwatch {
public:
    PosixStopwatch() { Start(); }
    void Start() override { start_time_ns_ = NowInNsecs(); }
    int64_t DurationInMsecs() override {
        return (NowInNsecs() - start_time_ns_) / kNsecsPerMsec;
    }

private:
    // Returns monotonic time in nanoseconds.
    uint64_t NowInNsecs() const {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * kNsecsPerSec + ts.tv_nsec;
    }

    uint64_t start_time_ns_;
    static constexpr uint64_t kNsecsPerMsec = 1000 * 1000;
    static constexpr uint64_t kNsecsPerSec = 1000 * 1000 * 1000;
};

} // namespace

int main(int argc, char** argv) {
    PosixStopwatch stopwatch;
    return runtests::DiscoverAndRunTests(&runtests::PosixRunTest, argc, argv,
                                         /*default_test_dirs=*/{}, &stopwatch,
                                         /*syslog_file_name=*/"");
}

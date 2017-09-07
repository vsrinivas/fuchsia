// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/errors.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>

static constexpr uint32_t kDefaultNumThreads = 4;
static constexpr float kDefaultMinWorkMsec = 5.0f;
static constexpr float kDefaultMaxWorkMsec = 15.0f;
static constexpr float kDefaultMinSleepMsec = 1.0f;
static constexpr float kDefaultMaxSleepMsec = 2.5f;

class LoadGeneratorThread :
    public fbl::SinglyLinkedListable<fbl::unique_ptr<LoadGeneratorThread>> {
public:
    LoadGeneratorThread(unsigned int seed) : seed_(seed) { }
    ~LoadGeneratorThread();

    mx_status_t Start();

    static float& min_work_msec() { return min_work_msec_; }
    static float& max_work_msec() { return max_work_msec_; }
    static float& min_sleep_msec() { return min_sleep_msec_; }
    static float& max_sleep_msec() { return max_sleep_msec_; }

private:
    int Run();

    double MakeRandomDouble(double min, double max);

    static float min_work_msec_;
    static float max_work_msec_;
    static float min_sleep_msec_;
    static float max_sleep_msec_;
    static volatile bool quit_;

    unsigned int seed_;
    bool thread_started_;
    thrd_t thread_;
    volatile double accumulator_;
};

float LoadGeneratorThread::min_work_msec_ = kDefaultMinWorkMsec;
float LoadGeneratorThread::max_work_msec_ = kDefaultMaxWorkMsec;
float LoadGeneratorThread::min_sleep_msec_ = kDefaultMinSleepMsec;
float LoadGeneratorThread::max_sleep_msec_ = kDefaultMaxSleepMsec;
volatile bool LoadGeneratorThread::quit_ = false;

LoadGeneratorThread::~LoadGeneratorThread() {
    if (thread_started_) {
        int musl_ret;
        quit_ = true;
        thrd_join(thread_, &musl_ret);
    }
}

mx_status_t LoadGeneratorThread::Start() {
    if (thread_started_) return MX_ERR_BAD_STATE;

    int c11_res = thrd_create(
            &thread_,
            [](void* ctx) -> int { return static_cast<LoadGeneratorThread*>(ctx)->Run(); },
            this);

    if (c11_res != thrd_success) {
        printf("Failed to create new client thread (res %d)!\n", c11_res);
        // TODO(johngro) : translate musl error
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

int LoadGeneratorThread::Run() {
    constexpr double kMinNum = 1.0;
    constexpr double kMaxNum = 100000000.0;
    uint32_t ticks_per_msec = static_cast<uint32_t>(mx_ticks_per_second() / 1000);
    accumulator_ = MakeRandomDouble(kMinNum, kMaxNum);

    // While it is not time to quit, waste time performing pointless double
    // precision floating point math.
    while (!quit_) {
        double work_delay = MakeRandomDouble(min_work_msec(), max_work_msec());
        uint64_t work_deadline_ticks = mx_ticks_get()
                                     + static_cast<mx_time_t>(work_delay * ticks_per_msec);

        while (!quit_ && (mx_ticks_get() < work_deadline_ticks)) {
            accumulator_ += MakeRandomDouble(kMinNum, kMaxNum);
            accumulator_ *= MakeRandomDouble(kMinNum, kMaxNum);
            accumulator_ -= MakeRandomDouble(kMinNum, kMaxNum);
            accumulator_ /= MakeRandomDouble(kMinNum, kMaxNum);

            double tmp = accumulator_;
            accumulator_  = fbl::clamp<double>(tmp, 0.0, kMaxNum);
        }

        if (quit_)
            break;

        double sleep_delay = MakeRandomDouble(min_sleep_msec(), max_sleep_msec());
        mx_time_t sleep_deadline = mx_time_get(MX_CLOCK_MONOTONIC)
                                 + static_cast<mx_time_t>(sleep_delay * 1000000.0);

        do {
            static constexpr mx_time_t max_sleep = MX_MSEC(10);
            mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);

            if (now >= sleep_deadline)
                break;

            if ((sleep_deadline - now) > max_sleep) {
                mx_nanosleep(now + max_sleep);
            } else {
                mx_nanosleep(sleep_deadline);
                break;
            }
        } while (!quit_);
    }

    return 0;
}

double LoadGeneratorThread::MakeRandomDouble(double min, double max) {
    double norm(rand_r(&seed_));
    norm /= fbl::numeric_limits<int>::max();
    return min + (norm * (max - min));
}

void usage(const char* program_name) {
    printf("usage: %s [N] [min_work max_work] [min_sleep max_sleep] [seed]\n"
           "  All arguments are positional and optional.\n"
           "  N             : Number of threads to create.  Default %u\n"
           "  min/max_work  : Min/max msec for threads to work for.  Default %.1f,%.1f mSec\n"
           "  min/max_sleep : Min/max msec for threads to sleep for.  Default %.1f,%.1f mSec\n"
           "  seed          : RNG seed to use.  Defaults to seeding from mx_time_get\n",
           program_name,
           kDefaultNumThreads,
           kDefaultMinWorkMsec,
           kDefaultMaxWorkMsec,
           kDefaultMinSleepMsec,
           kDefaultMaxSleepMsec);
}

int main(int argc, char** argv) {
    auto show_usage = fbl::MakeAutoCall([argv]() { usage(argv[0]); });

    // 0, 1, 3, 5 and 6 arguments are the only legal number of args.
    switch (argc) {
    case 1:
    case 2:
    case 4:
    case 6:
    case 7:
        break;
    default:
        return -1;
    }

    // Parse and sanity check number of threads, if present.
    uint32_t num_threads = kDefaultNumThreads;
    if (argc >= 2) {
        if (sscanf(argv[1], "%u", &num_threads) != 1) return -1;
        if (num_threads == 0) return -1;
    }

    // Parse and sanity check min/max work times, if present.
    if (argc >= 4) {
        if (sscanf(argv[2], "%f", &LoadGeneratorThread::min_work_msec()) != 1) return -1;
        if (sscanf(argv[3], "%f", &LoadGeneratorThread::max_work_msec()) != 1) return -1;
        if (LoadGeneratorThread::min_work_msec() <= 0.0f) return -1;
        if (LoadGeneratorThread::min_work_msec() > LoadGeneratorThread::max_work_msec()) return -1;
    }

    // Parse and sanity check min/max sleep times, if present.
    if (argc >= 6) {
        if (sscanf(argv[4], "%f", &LoadGeneratorThread::min_sleep_msec()) != 1) return -1;
        if (sscanf(argv[5], "%f", &LoadGeneratorThread::max_sleep_msec()) != 1) return -1;
        if (LoadGeneratorThread::min_sleep_msec() <= 0.0f) return -1;
        if (LoadGeneratorThread::min_sleep_msec() > LoadGeneratorThread::max_sleep_msec())
            return -1;
    }

    // Parse the PRNG seed, if present.
    unsigned int seed = static_cast<unsigned int>(mx_time_get(CLOCK_MONOTONIC));
    if (argc >= 7) {
        if (sscanf(argv[6], "%u", &seed) != 1) return -1;
    }

    // Argument parsing checks out, cancel the showing of the usage message.
    show_usage.cancel();

    printf("Creating %u load generation thread%s.\n"
           "Work times  : [%.3f, %.3f] mSec\n"
           "Sleep times : [%.3f, %.3f] mSec\n"
           "Seed        : %u\n",
           num_threads,
           num_threads == 1 ? "" : "s",
           LoadGeneratorThread::min_work_msec(),
           LoadGeneratorThread::max_work_msec(),
           LoadGeneratorThread::min_sleep_msec(),
           LoadGeneratorThread::max_sleep_msec(),
           seed);

    fbl::SinglyLinkedList<fbl::unique_ptr<LoadGeneratorThread>> threads;
    for (uint32_t i = 0; i < num_threads; ++i) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<LoadGeneratorThread> t(new (&ac) LoadGeneratorThread(rand_r(&seed)));

        if (!ac.check()) {
            printf("Failed to create thread %u/%u\n", i + 1, num_threads);
            return -1;
        }

        threads.push_front(fbl::move(t));
    }

    for (auto& t : threads) {
        mx_status_t res = t.Start();
        if (res != MX_OK) {
            printf("Failed to start thread.  (res %d)\n", res);
            return res;
        }
    }

    printf("Running.  Press any key to exit\n");
    char junk;
    ::read(STDIN_FILENO, &junk, sizeof(junk));

    printf("Shutting down...\n");
    threads.clear();
    printf("Finished\n");

    return 0;
}

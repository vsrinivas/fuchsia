// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <array>
#include <memory>

#include "global_stats.h"
#include "random.h"
#include "sync_obj.h"
#include "test_thread.h"

namespace {
const std::array kThreadBehaviors{
    TestThreadBehavior{},
    TestThreadBehavior{},
    TestThreadBehavior{
        .profile_type = ProfileType::Fair,
        .priority = ZX_PRIORITY_HIGH,
        .intermediate_linger{
            .linger_probability = 0.5f,
            .time_dist{ZX_USEC(100), ZX_USEC(200)},
        },
        .final_linger{
            .linger_probability = 0.5f,
            .time_dist{ZX_USEC(100), ZX_MSEC(2)},
        },
    },
    TestThreadBehavior{
        .profile_type = ProfileType::Fair,
        .priority = ZX_PRIORITY_LOW,
        .intermediate_linger{
            .linger_probability = 0.5f,
            .time_dist{ZX_USEC(100), ZX_MSEC(2)},
        },
        .final_linger{
            .linger_probability = 1.0f,
            .time_dist{ZX_MSEC(5), ZX_MSEC(20)},
        },
    },
    TestThreadBehavior{
        .profile_type = ProfileType::Deadline,
        .period = ZX_MSEC(10),
        .deadline = ZX_MSEC(5),
        .capacity = ZX_MSEC(2),
        .final_linger{
            .linger_probability = 1.0f,
            .time_dist{ZX_USEC(100), ZX_USEC(1950)},
        },
    },
    TestThreadBehavior{
        .profile_type = ProfileType::Deadline,
        .period = ZX_MSEC(10),
        .deadline = ZX_MSEC(5),
        .capacity = ZX_MSEC(2),
        .final_linger{
            .linger_probability = 1.0f,
            .time_dist{ZX_USEC(1500), ZX_MSEC(4)},
        },
    },
    TestThreadBehavior{
        .profile_type = ProfileType::Deadline,
        .period = ZX_MSEC(1),
        .deadline = ZX_MSEC(1),
        .capacity = ZX_USEC(800),
        .final_linger{
            .linger_probability = 1.0f,
            .time_dist{ZX_USEC(100), ZX_USEC(500)},
        },
    },
    TestThreadBehavior{
        .profile_type = ProfileType::Deadline,
        .period = ZX_MSEC(1),
        .deadline = ZX_MSEC(1),
        .capacity = ZX_USEC(800),
        .intermediate_linger{
            .linger_probability = 0.3f,
            .time_dist{ZX_USEC(50), ZX_USEC(100)},
        },
        .final_linger{
            .linger_probability = 0.8f,
            .time_dist{ZX_USEC(100), ZX_USEC(150)},
        },
    },
};
}

int main(int argc, char** argv) {
  // Initialize our TestThread statics
  if (const zx_status_t status = TestThread::InitStatics(); status != ZX_OK) {
    printf("Failed to initialize TestThread statics (status %d)!\n", status);
    return status;
  }

  // put stdin into non-blocking mode.
  //
  // TODO(johngro): Look into fixing this.  This seems to work for simple
  // programs, but components launched from the command line using `run` do not
  // seem to have stdin connected to the component properly.
  int stdin_bits = fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK;
  fcntl(STDIN_FILENO, F_SETFL, stdin_bits | O_NONBLOCK);

  // Make sure that we shut down all of our threads and put stdin back the way
  // it was when we exit.
  auto cleanup = fit::defer([stdin_bits]() {
    printf("Shutting down\n");
    fcntl(STDIN_FILENO, F_SETFL, stdin_bits & ~O_NONBLOCK);
    TestThread::Shutdown();
    printf("done\n");
  });

  // Create the profiles and threads
  for (const auto& behavior : kThreadBehaviors) {
    if (TestThread::AddThread(behavior)) {
      printf("Failed to create test thread\n");
    }
  }

  // Now start them all up.
  printf("Starting threads\n");
  for (auto& t : TestThread::threads()) {
    t->Start();
  }

  printf("Running until keypress\n");
  zx::time start{zx::clock::get_monotonic()};
  bool quit_now = false;

  // Choose a thread and meddle with its profile at a rate of [20, 100] Hz
  std::uniform_int_distribution<zx_duration_t> change_profile_delay_dist{ZX_MSEC(10), ZX_MSEC(50)};
  zx::time status_deadline = zx::clock::get_monotonic();
  zx::time change_profile_deadline =
      zx::deadline_after(zx::nsec(Random::Get(change_profile_delay_dist)));

  while (!quit_now) {
    zx::time now = zx::clock::get_monotonic();

    if (getchar() >= 0) {
      break;
    };

    if (now >= status_deadline) {
      printf(
          "%10.3lf sec : m.acq %lu m.rel %lu m.timeout %lu cv.acq %lu cv.rel %lu cv.wait %lu "
          "cv.timeout %lu cv.sig %lu cv.bcast %lu int.spin %lu int.sleep %lu fin.spin %lu "
          "fin.sleep %lu p.change %lu p.revert %lu\n",
          static_cast<double>((zx::clock::get_monotonic() - start).to_nsecs()) / ZX_SEC(1),
          global_stats.mutex_acquires.load(), global_stats.mutex_releases.load(),
          global_stats.mutex_acq_timeouts.load(), global_stats.condvar_acquires.load(),
          global_stats.condvar_releases.load(), global_stats.condvar_waits.load(),
          global_stats.condvar_acq_timeouts.load(), global_stats.condvar_signals.load(),
          global_stats.condvar_bcasts.load(), global_stats.intermediate_spins.load(),
          global_stats.intermediate_sleeps.load(), global_stats.final_spins.load(),
          global_stats.final_sleeps.load(), global_stats.profiles_changed.load(),
          global_stats.profiles_reverted.load());
      status_deadline += zx::sec(1);
    }

    if (now >= change_profile_deadline) {
      TestThread::random_thread().ChangeProfile();
      change_profile_deadline += zx::nsec(Random::Get(change_profile_delay_dist));
    }

    zx::nanosleep(std::min(status_deadline, change_profile_deadline));
  }
  printf("keypress\n");
}

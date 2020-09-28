// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <pthread.h>
#include <zircon/assert.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <thread>

namespace {

struct State {
  pthread_barrier_t start_barrier;
  pthread_barrier_t stop_barrier;
  const size_t number_of_switches;

  State(size_t thread_count, size_t number_of_switches) : number_of_switches(number_of_switches) {
    FX_CHECK(0 == pthread_barrier_init(&start_barrier, nullptr,
                                       thread_count + 1));  // additional thread for main
    FX_CHECK(0 == pthread_barrier_init(&stop_barrier, nullptr,
                                       thread_count + 1));  // additional thread for main
  }
};

class ProfileService {
 public:
  bool Init() {
    std::shared_ptr<sys::ServiceDirectory> svc = sys::ServiceDirectory::CreateFromNamespace();
    if (svc == nullptr) {
      FX_LOGS(ERROR) << "No service directory";
      return false;
    }

    zx_status_t status = svc->Connect(provider_.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Couldn't connect to service: " << zx_status_get_string(status);
      return false;
    }

    return true;
  }

  zx_status_t ApplyAffinityProfile(size_t cpu_num, std::thread* thread_a, std::thread* thread_b) {
    FX_CHECK(cpu_num < 31);
    uint32_t mask = 1 << cpu_num;

    zx::profile profile;
    zx_status_t server_status;
    zx_status_t status = provider_->GetCpuAffinityProfile({mask}, &server_status, &profile);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to contact profile service: " << zx_status_get_string(status);
      return status;
    }
    if (server_status != ZX_OK) {
      FX_LOGS(ERROR) << "Profile service failure: " << zx_status_get_string(server_status);
      return server_status;
    }

    status = HandleFromThread(thread_a)->set_profile(profile, /*options=*/0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to set profile: " << zx_status_get_string(status);
      return status;
    }

    status = HandleFromThread(thread_b)->set_profile(profile, /*options=*/0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to set profile: " << zx_status_get_string(status);
      return status;
    }

    return ZX_OK;
  }

 private:
  zx::unowned<zx::thread> HandleFromThread(std::thread* thread) {
    zx_handle_t handle = native_thread_get_zx_handle(thread->native_handle());
    return zx::unowned<zx::thread>(handle);
  }

  fuchsia::scheduler::ProfileProviderSyncPtr provider_;
};

void ThreadPair(size_t cpu_num, State* state, ProfileService* profiles) {
  auto thread_action = [state](zx::eventpair event, bool first) {
    auto wait_val = pthread_barrier_wait(&state->start_barrier);
    FX_CHECK(wait_val == PTHREAD_BARRIER_SERIAL_THREAD || wait_val == 0);

    size_t to_receive = state->number_of_switches;

    if (first) {
      FX_CHECK(ZX_OK == event.signal_peer(0, ZX_USER_SIGNAL_0));
    }

    while (to_receive > 0) {
      FX_CHECK(ZX_OK == event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));
      to_receive--;
      FX_CHECK(ZX_OK == event.signal(ZX_USER_SIGNAL_0, 0));
      FX_CHECK(ZX_OK == event.signal_peer(0, ZX_USER_SIGNAL_0));
    }

    wait_val = pthread_barrier_wait(&state->stop_barrier);
    FX_CHECK(wait_val == PTHREAD_BARRIER_SERIAL_THREAD || wait_val == 0);
  };

  zx::eventpair e1, e2;
  FX_CHECK(ZX_OK == zx::eventpair::create(0, &e1, &e2));

  std::thread thread_a(thread_action, std::move(e1), true);
  std::thread thread_b(thread_action, std::move(e2), false);

  FX_CHECK(ZX_OK == profiles->ApplyAffinityProfile(cpu_num, &thread_a, &thread_b));

  thread_a.detach();
  thread_b.detach();
}

}  // namespace

const char kMessage[] = "ping";
const size_t kMessageSize = 4;

int main(int argc, char** argv) {
  zx::channel incoming(zx_take_startup_handle(PA_USER0));

  if (!incoming) {
    printf("ERROR: Invalid incoming handle\n");
    return 1;
  }

  size_t cpus = zx_system_get_num_cpus();
  uint64_t number_of_switches = 0;

  // Signal that this process is ready to accept instructions.
  FX_CHECK(ZX_OK == incoming.write(0, kMessage, kMessageSize, nullptr, 0));

  ProfileService profiles;
  FX_CHECK(profiles.Init());

  while (true) {
    // Read the number of context switches to perform.
    FX_CHECK(ZX_OK == incoming.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                        zx::time::infinite(), nullptr));
    if (ZX_OK != incoming.read(0, &number_of_switches, nullptr, sizeof(number_of_switches), 0,
                               nullptr, nullptr)) {
      break;
    }
    State state(cpus * 2, number_of_switches);

    // Initialize all thread pairs with the state.
    for (size_t i = 0; i < cpus; i++) {
      ThreadPair(i, &state, &profiles);
    }

    // Wait until all threads are ready to start, then signal to the test that we have started.
    auto wait_val = pthread_barrier_wait(&state.start_barrier);
    FX_CHECK(wait_val == PTHREAD_BARRIER_SERIAL_THREAD || wait_val == 0);
    FX_CHECK(ZX_OK == incoming.write(0, kMessage, kMessageSize, nullptr, 0));

    // Wait until all threads have completed, then signal to the test that we have finished.
    wait_val = pthread_barrier_wait(&state.stop_barrier);
    FX_CHECK(wait_val == PTHREAD_BARRIER_SERIAL_THREAD || wait_val == 0);
    FX_CHECK(ZX_OK == incoming.write(0, kMessage, kMessageSize, nullptr, 0));
  }

  return 0;
}

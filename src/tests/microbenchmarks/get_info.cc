// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <string>
#include <thread>
#include <vector>

#include <perftest/perftest.h>

#include "assert.h"
#include "lib/zx/time.h"

namespace {

constexpr const char* kPath = "/bin/get_info_helper";

// Measure the time taken by various zx_object_get_info() calls on collections of processes and
// threads.  Specifically:
//  - zx_object_get_info()/ZX_INFO_TASK_RUNTIME on a process
//  - zx_object_get_info()/ZX_INFO_TASK_RUNTIME on a job
//  - zx_object_get_info()/ZX_INFO_JOB_PROCESSES + zx_object_get_child() for fetching handles for
//  all the processes in the job
//  - zx_object_get_info()/ZX_INFO_PROCESS_THREADS + zx_object_get_info() for fetching handles for
//  all threads in the processes.
//  - zx_object_get_info()/ZX_INFO_TASK_RUNTIME on all threads.
bool GetRuntimeInfoTest(perftest::RepeatState* state, size_t processes, size_t threads) {
  ZX_ASSERT(processes > 0);
  std::string run_str = std::to_string(threads);
  const char* const argv[] = {kPath, run_str.c_str(), nullptr};

  zx::job job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));

  std::vector<zx::handle> process_list;

  for (size_t i = 0; i < processes; i++) {
    zx::channel chan1, chan2;
    ASSERT_OK(zx::channel::create(0, &chan1, &chan2));
    zx_handle_t server_handle = chan2.release();
    zx::handle process;
    fdio_spawn_action_t actions[1] = {
        {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = PA_USER0, .handle = server_handle}}};
    ASSERT_OK(fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, kPath, argv, nullptr /* environ */,
                             1 /* action count */, actions, process.reset_and_get_address(),
                             nullptr /* err message out */));

    char out[1024];
    uint32_t len;

    ASSERT_OK(chan1.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                             nullptr));
    ASSERT_OK(chan1.read(0, &out, nullptr, sizeof(out), 0, &len, nullptr));
    process_list.emplace_back(std::move(process));
  }

  state->DeclareStep("process");
  state->DeclareStep("job");
  state->DeclareStep("enumerate_job");
  state->DeclareStep("enumerate_processes");
  state->DeclareStep("threads");

  zx_info_task_runtime_t info;
  while (state->KeepRunning()) {
    ASSERT_OK(
        process_list[0].get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
    state->NextStep();
    ASSERT_OK(job.get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
    state->NextStep();

    zx_koid_t process_koids[128];
    zx::process process_handles[128];
    zx::thread thread_handles[128];
    size_t thread_count = 0;
    size_t process_count = 0;
    ASSERT_OK(job.get_info(ZX_INFO_JOB_PROCESSES, process_koids, sizeof(process_koids),
                           &process_count, nullptr));
    for (size_t i = 0; i < process_count; i++) {
      ASSERT_OK(job.get_child(process_koids[i], ZX_RIGHT_SAME_RIGHTS, &process_handles[i]));
    }
    state->NextStep();

    for (size_t i = 0; i < process_count; i++) {
      size_t count;
      zx_koid_t thread_koids[128];
      ASSERT_OK(process_handles[i].get_info(ZX_INFO_PROCESS_THREADS, thread_koids,
                                            sizeof(thread_koids), &count, nullptr));
      for (size_t j = 0; j < count; j++) {
        ASSERT_OK(process_handles[i].get_child(thread_koids[j], ZX_RIGHT_SAME_RIGHTS,
                                               &thread_handles[thread_count++]));
      }
    }
    state->NextStep();

    for (size_t i = 0; i < thread_count; i++) {
      ASSERT_OK(
          thread_handles[i].get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
    }
  }

  ASSERT_OK(job.kill());
  ASSERT_OK(job.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));

  return true;
}

// Measures the time to call zx_object_get_info()/ZX_INFO_TASK_RUNTIME on the current thread.
bool GetRuntimeInfoThread(perftest::RepeatState* state) {
  auto self = zx::thread::self();

  zx_info_task_runtime_t info;
  while (state->KeepRunning()) {
    ASSERT_OK(self->get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  }

  return true;
}

// Measures the time to call zx_object_get_info()/ZX_INFO_TASK_RUNTIME on the current process while
// two threads rapidly context switch.
bool GetRuntimeInfoThreadsConcurrent(perftest::RepeatState* state) {
  zx::eventpair e1, e2;
  ASSERT_OK(zx::eventpair::create(0, &e1, &e2));

  pthread_barrier_t start_barrier;
  pthread_barrier_init(&start_barrier, nullptr, 3);

  std::atomic<bool> done = false;

  auto thread_action = [&](zx::eventpair* event, bool first) {
    pthread_barrier_wait(&start_barrier);
    if (first) {
      ASSERT_OK(event->signal_peer(0, ZX_USER_SIGNAL_0));
    }

    do {
      ASSERT_OK(event->wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));
      ASSERT_OK(event->signal(ZX_USER_SIGNAL_0, 0));
      ASSERT_OK(event->signal_peer(0, ZX_USER_SIGNAL_0));
    } while (!done);
  };

  std::thread t1(thread_action, &e1, true);
  std::thread t2(thread_action, &e2, false);

  pthread_barrier_wait(&start_barrier);

  auto self = zx::process::self();
  zx_info_task_runtime_t info;
  while (state->KeepRunning()) {
    ASSERT_OK(self->get_info(ZX_INFO_TASK_RUNTIME, &info, sizeof(info), nullptr, nullptr));
  }

  done = true;
  t1.join();
  t2.join();

  return true;
}

void RegisterTests() {
  perftest::RegisterTest("GetInfo/Runtime/P=1/T=1", GetRuntimeInfoTest, 1, 1);
  perftest::RegisterTest("GetInfo/Runtime/P=1/T=10", GetRuntimeInfoTest, 1, 10);
  perftest::RegisterTest("GetInfo/Runtime/P=10/T=1", GetRuntimeInfoTest, 10, 1);
  perftest::RegisterTest("GetInfo/Runtime/P=10/T=10", GetRuntimeInfoTest, 10, 10);
  perftest::RegisterTest("GetInfo/Runtime/ThreadOnly", GetRuntimeInfoThread);
  perftest::RegisterTest("GetInfo/Runtime/ConcurrentThreads", GetRuntimeInfoThreadsConcurrent);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace

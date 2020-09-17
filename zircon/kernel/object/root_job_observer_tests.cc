// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <object/dispatcher.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/root_job_observer.h>
#include <object/vm_address_region_dispatcher.h>

namespace {

// Create a suspended thread inside the given process.
KernelHandle<ThreadDispatcher> CreateThread(fbl::RefPtr<ProcessDispatcher> parent_process) {
  fbl::RefPtr<ThreadDispatcher> child_thread;
  KernelHandle<ThreadDispatcher> thread_handle;
  zx_rights_t thread_rights;
  ASSERT(ThreadDispatcher::Create(parent_process, 0, "unittest_thread", &thread_handle,
                                  &thread_rights) == ZX_OK);
  child_thread = thread_handle.dispatcher();
  ASSERT(child_thread->Initialize() == ZX_OK);
  ASSERT(child_thread->Suspend() == ZX_OK);
  ASSERT(child_thread->Start(ThreadDispatcher::EntryState{}, /*initial_thread=*/true) == ZX_OK);

  return thread_handle;
}

// Create a process inside the given job.
KernelHandle<ProcessDispatcher> CreateProcess(fbl::RefPtr<JobDispatcher> parent_job) {
  fbl::RefPtr<ProcessDispatcher> child_process;
  KernelHandle<ProcessDispatcher> process_handle;
  KernelHandle<VmAddressRegionDispatcher> vmar_handle;
  zx_rights_t rights, vmar_rights;
  ASSERT(ProcessDispatcher::Create(parent_job, "unittest_process", 0, &process_handle, &rights,
                                   &vmar_handle, &vmar_rights) == ZX_OK);
  return process_handle;
}

// Exercise basic creation/destruction of the RootJobObserver.
bool TestCreateDestroy() {
  BEGIN_TEST;

  // Create and destroy the root job observer.
  Event callback_fired;
  fbl::RefPtr<JobDispatcher> root_job = JobDispatcher::CreateRootJob();
  RootJobObserver observer{root_job, nullptr, [&]() { callback_fired.Signal(); }};

  // Ensure the callback fired.
  callback_fired.Wait();

  END_TEST;
}

// Ensure that the callback fires when the root job is killed.
bool TestCallbackFiresOnRootJobDeath() {
  BEGIN_TEST;

  Event root_job_killed;

  // Create the root job with a child process, and start watching it.
  fbl::RefPtr<JobDispatcher> root_job = JobDispatcher::CreateRootJob();
  KernelHandle<ProcessDispatcher> child_process = CreateProcess(root_job);
  RootJobObserver observer{root_job, nullptr, [&root_job_killed]() { root_job_killed.Signal(); }};

  // Shouldn't be signalled yet.
  EXPECT_EQ(root_job_killed.Wait(Deadline::after(ZX_MSEC(1))), ZX_ERR_TIMED_OUT);

  // Kill the root job.
  ASSERT_TRUE(root_job->Kill(1));

  // Ensure we are signalled.
  EXPECT_EQ(root_job_killed.Wait(), ZX_OK);

  END_TEST;
}

// Test that by the time the RootJobObserver callback fires due to the
// root job being killed, all of the root job's children have already
// been terminated.
bool TestChildrenAlreadyDeadWhenCallbackFires() {
  BEGIN_TEST;

  // Create a new root job, containing a process and a thread.
  fbl::RefPtr<JobDispatcher> root_job = JobDispatcher::CreateRootJob();
  KernelHandle<ProcessDispatcher> child_process = CreateProcess(root_job);
  KernelHandle<ThreadDispatcher> child_thread = CreateThread(child_process.dispatcher());

  // Create a root job observer. The callback ensures that the child process and thread
  // are both dead when it fires.
  Event callback_fired;
  RootJobObserver observer{
      root_job, nullptr, [&]() {
        ASSERT(child_process.dispatcher()->state() == ProcessDispatcher::State::DEAD);
        ASSERT(child_thread.dispatcher()->IsDyingOrDead());
        callback_fired.Signal();
      }};

  // Ensure everything is running.
  ASSERT_EQ(child_process.dispatcher()->state(), ProcessDispatcher::State::RUNNING);
  ASSERT_FALSE(child_thread.dispatcher()->IsDyingOrDead());

  // Kill the parent job.
  ASSERT_TRUE(root_job->Kill(1));

  // Wait for the callback to fire.
  callback_fired.Wait();

  END_TEST;
}

// Ensure that the RootJobObserver callback fires when the root job has
// no children, even if the root job itself is not killed.
bool TestCallbackFiresWhenNoChildren() {
  BEGIN_TEST;

  // Create a new root job, containing a process and a thread.
  fbl::RefPtr<JobDispatcher> root_job = JobDispatcher::CreateRootJob();
  KernelHandle<ProcessDispatcher> child_process = CreateProcess(root_job);
  KernelHandle<ThreadDispatcher> child_thread = CreateThread(child_process.dispatcher());

  // Create a root job observer. The callback ensures that the child process and thread
  // are both dead when it fires.
  Event callback_fired;
  RootJobObserver observer{root_job, nullptr, [&]() { callback_fired.Signal(); }};

  // Ensure everything is running.
  ASSERT_EQ(child_process.dispatcher()->state(), ProcessDispatcher::State::RUNNING);
  ASSERT_FALSE(child_thread.dispatcher()->IsDyingOrDead());

  // Kill the process.
  child_process.dispatcher()->Kill(1);

  // Ensure the callback fires.
  callback_fired.Wait();

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(root_job_observer)
UNITTEST("CreateDestroy", TestCreateDestroy)
UNITTEST("CallbackFiresOnRootJobDeath", TestCallbackFiresOnRootJobDeath)
UNITTEST("ChildrenAlreadyDeadWhenCallbackFires", TestChildrenAlreadyDeadWhenCallbackFires)
UNITTEST("CallbackFiresWhenNoChildren", TestCallbackFiresWhenNoChildren)
UNITTEST_END_TESTCASE(root_job_observer, "root_job_observer", "RootJobObserver tests")

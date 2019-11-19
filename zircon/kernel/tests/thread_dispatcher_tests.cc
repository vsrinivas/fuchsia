// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

static bool test_create_destroy_thread_no_init() {
  BEGIN_TEST;

  KernelHandle<JobDispatcher> job;
  zx_rights_t job_rights;
  auto status = JobDispatcher::Create(0u, GetRootJobDispatcher(), &job, &job_rights);
  ASSERT_EQ(status, ZX_OK, "job created");

  KernelHandle<ProcessDispatcher> process;
  KernelHandle<VmAddressRegionDispatcher> vmar;
  zx_rights_t process_rights;
  zx_rights_t vmar_rights;
  status = ProcessDispatcher::Create(job.dispatcher(), "k-ut-p0", 0u, &process, &process_rights,
                                     &vmar, &vmar_rights);
  ASSERT_EQ(status, ZX_OK, "process created");

  KernelHandle<ThreadDispatcher> thread;
  zx_rights_t thread_rights;
  status = ThreadDispatcher::Create(process.dispatcher(), 0u, "k-ut-t0", &thread, &thread_rights);
  ASSERT_EQ(status, ZX_OK, "thread created");

  END_TEST;
}

static bool test_create_init_destroy_thread() {
  BEGIN_TEST;

  KernelHandle<JobDispatcher> job;
  zx_rights_t job_rights;
  auto status = JobDispatcher::Create(0u, GetRootJobDispatcher(), &job, &job_rights);
  ASSERT_EQ(status, ZX_OK, "job created");

  KernelHandle<ProcessDispatcher> process;
  KernelHandle<VmAddressRegionDispatcher> vmar;
  zx_rights_t process_rights;
  zx_rights_t vmar_rights;
  status = ProcessDispatcher::Create(job.dispatcher(), "k-ut-p1", 0u, &process, &process_rights,
                                     &vmar, &vmar_rights);
  ASSERT_EQ(status, ZX_OK, "process created");

  KernelHandle<ThreadDispatcher> thread;
  zx_rights_t thread_rights;
  status = ThreadDispatcher::Create(process.dispatcher(), 0u, "k-ut-t1", &thread, &thread_rights);
  ASSERT_EQ(status, ZX_OK, "thread created");

  status = thread.dispatcher()->Initialize();
  ASSERT_EQ(status, ZX_OK, "thread init");

  END_TEST;
}

UNITTEST_START_TESTCASE(thread_dispatcher)
UNITTEST("test create destroy thread", test_create_destroy_thread_no_init)
UNITTEST("test create init destroy thread", test_create_init_destroy_thread)
UNITTEST_END_TESTCASE(thread_dispatcher, "thread_dispatcher", "Dispatcher objec tests")

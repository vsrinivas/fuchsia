// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/job.h>

#include <zxtest/zxtest.h>

// Tests in this file rely that the default job is the root job.

static void retrieve_mem_event(zx_system_event_type_t event_type) {
  zx::event mem_event;

  ASSERT_EQ(zx_system_get_event(ZX_HANDLE_INVALID, event_type, mem_event.reset_and_get_address()),
            ZX_ERR_BAD_HANDLE, "cannot get with invalid root job");

  ASSERT_EQ(zx_system_get_event(zx_process_self(), event_type, mem_event.reset_and_get_address()),
            ZX_ERR_WRONG_TYPE, "cannot get without a job handle");

  zx::job tmp_job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &tmp_job), ZX_OK, "helper sub job");

  ASSERT_EQ(zx_system_get_event(tmp_job.get(), event_type, mem_event.reset_and_get_address()),
            ZX_ERR_ACCESS_DENIED, "cannot get without correct root job");

  zx::unowned_job root_job(zx_job_default());
  if (!root_job->is_valid()) {
    printf("no root job. skipping part of test\n");
  } else {
    ASSERT_EQ(zx_system_get_event(root_job->get(), ~0u, mem_event.reset_and_get_address()),
              ZX_ERR_INVALID_ARGS, "incorrect kind value does not retrieve");

    ASSERT_EQ(zx_system_get_event(root_job->get(), event_type, mem_event.reset_and_get_address()),
              ZX_OK, "can get if root provided");

    // Confirm we at least got an event.
    zx_info_handle_basic_t info;
    ASSERT_EQ(zx_object_get_info(mem_event.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                 nullptr, nullptr),
              ZX_OK, "object_get_info");
    EXPECT_NE(info.koid, 0, "no koid");
    EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT, "incorrect type");
    EXPECT_EQ(info.rights, ZX_DEFAULT_SYSTEM_EVENT_LOW_MEMORY_RIGHTS, "incorrect rights");
  }
}

static void signal_mem_event_from_userspace(zx_system_event_type_t event_type) {
  zx::unowned_job root_job(zx_job_default());
  if (!root_job->is_valid()) {
    printf("no root job. skipping test\n");
  } else {
    zx::event mem_event;
    ASSERT_EQ(zx_system_get_event(root_job->get(), event_type, mem_event.reset_and_get_address()),
              ZX_OK);

    ASSERT_EQ(mem_event.signal(0, 1), ZX_ERR_ACCESS_DENIED, "shouldn't be able to signal");
  }
}

TEST(SystemEvent, RetrieveOom) { retrieve_mem_event(ZX_SYSTEM_EVENT_OUT_OF_MEMORY); }

TEST(SystemEvent, CannotSignalOomFromUserspace) {
  signal_mem_event_from_userspace(ZX_SYSTEM_EVENT_OUT_OF_MEMORY);
}

TEST(SystemEvent, RetrieveMempressureCritical) {
  retrieve_mem_event(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL);
}

TEST(SystemEvent, CannotSignalMempressureCriticalFromUserspace) {
  signal_mem_event_from_userspace(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL);
}

TEST(SystemEvent, RetreiveMempressureWarning) {
  retrieve_mem_event(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING);
}

TEST(SystemEvent, CannotSignalMempressureWarningFromUserspace) {
  signal_mem_event_from_userspace(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING);
}

TEST(SystemEvent, RetrieveMempressureNormal) {
  retrieve_mem_event(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL);
}

TEST(SystemEvent, CannotSignalMempressureNormalFromUserspace) {
  signal_mem_event_from_userspace(ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL);
}

TEST(SystemEvent, ExactlyOneMemoryEventSignaled) {
  zx::unowned_job root_job(zx_job_default());
  if (!root_job->is_valid()) {
    printf("no root job. skipping part of test\n");
    return;
  }

  const int kNumEvents = 4;
  zx::event mem_events[kNumEvents];
  zx_wait_item_t wait_items[kNumEvents];
  zx_system_event_type_t types[kNumEvents] = {
      ZX_SYSTEM_EVENT_OUT_OF_MEMORY, ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL,
      ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING, ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL};

  for (int i = 0; i < kNumEvents; i++) {
    ASSERT_EQ(zx_system_get_event(root_job->get(), types[i], mem_events[i].reset_and_get_address()),
              ZX_OK, "can get if root provided");
    wait_items[i].handle = mem_events[i].get();
    wait_items[i].waitfor = ZX_EVENT_SIGNALED;
    wait_items[i].pending = 0;
  }

  // This wait should return immediately.
  ASSERT_EQ(zx_object_wait_many(wait_items, kNumEvents, ZX_TIME_INFINITE), ZX_OK,
            "wait on memory events");

  int events_signaled = 0;
  for (auto& wait_item : wait_items) {
    if (wait_item.pending) {
      events_signaled++;
    }
  }

  ASSERT_EQ(events_signaled, 1, "exactly one memory event signaled");
}

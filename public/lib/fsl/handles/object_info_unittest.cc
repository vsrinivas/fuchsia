// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/handles/object_info.h"

#include <thread>

#include <zircon/process.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>
#include <zx/channel.h>
#include <zx/event.h>

#include "gtest/gtest.h"

namespace fsl {
namespace {

TEST(ObjectInfo, GetKoidOfInvalidHandle) {
  EXPECT_EQ(ZX_KOID_INVALID, GetKoid(ZX_HANDLE_INVALID));
}

TEST(ObjectInfo, GetKoidOfDistinctObjects) {
  zx::event event1, event2;
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event1));
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event2));

  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event2.get()));
  EXPECT_NE(GetKoid(event1.get()), GetKoid(event2.get()));
}

TEST(ObjectInfo, GetKoidOfDuplicates) {
  zx::event event1, event2;
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event1));
  ASSERT_EQ(ZX_OK, event1.duplicate(ZX_RIGHT_SAME_RIGHTS, &event2));

  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_EQ(GetKoid(event1.get()), GetKoid(event2.get()));
}

TEST(ObjectInfo, GetRelatedKoidOfChannel) {
  zx::channel channel1, channel2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0u, &channel1, &channel2));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(channel1.get()));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(channel2.get()));

  EXPECT_EQ(GetKoid(channel2.get()), GetRelatedKoid(channel1.get()));
  EXPECT_EQ(GetKoid(channel1.get()), GetRelatedKoid(channel2.get()));
}

TEST(ObjectInfo, GetRelatedKoidOfEvent) {
  zx::event event1;
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event1));
  EXPECT_NE(ZX_KOID_INVALID, GetKoid(event1.get()));
  EXPECT_EQ(ZX_KOID_INVALID, GetRelatedKoid(event1.get()));
}

TEST(ObjectInfo, GetNameOfInvalidHandle) {
  EXPECT_EQ(std::string(), GetObjectName(ZX_HANDLE_INVALID));
}

TEST(ObjectInfo, SetNameOfInvalidHandle) {
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, SetObjectName(ZX_HANDLE_INVALID, "foo"));
}

TEST(ObjectInfo, GetCurrentProcessKoid) {
  EXPECT_NE(ZX_KOID_INVALID, GetCurrentProcessKoid());
}

TEST(ObjectInfo, GetAndSetNameOfCurrentProcess) {
  zx_handle_t process_handle = zx_process_self();
  std::string old_name = GetObjectName(process_handle);
  std::string new_name = "set-process-name-test";

  EXPECT_EQ(ZX_OK, SetObjectName(process_handle, new_name));
  EXPECT_EQ(new_name, GetObjectName(process_handle));
  EXPECT_EQ(new_name, GetCurrentProcessName());

  SetObjectName(process_handle, old_name);
}

TEST(ObjectInfo, GetCurrentThreadKoid) {
  zx_koid_t self_koid = GetCurrentThreadKoid();
  EXPECT_NE(ZX_KOID_INVALID, self_koid);

  zx_koid_t thread_koid = ZX_KOID_INVALID;
  std::thread thread([&thread_koid] { thread_koid = GetCurrentThreadKoid(); });
  thread.join();

  EXPECT_NE(ZX_KOID_INVALID, thread_koid);
  EXPECT_NE(self_koid, thread_koid);
}

TEST(ObjectInfo, GetAndSetNameOfCurrentThread) {
  std::string old_name = GetCurrentThreadName();
  std::string new_name = "set-thread-name-test";

  EXPECT_EQ(ZX_OK, SetCurrentThreadName(new_name));
  EXPECT_EQ(new_name, GetCurrentThreadName());

  SetCurrentThreadName(old_name);
}

}  // namespace
}  // namespace fsl

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

namespace object_info_test {
namespace {

template <typename HandleType>
uint32_t GetHandleCountOrZero(const HandleType& handle) {
  zx_info_handle_count_t info;

  if (handle.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return 0u;
  }

  return info.handle_count;
}

TEST(ObjectGetInfoTest, OpenValidHandleSuceeds) {
  EXPECT_OK(zx::process::self()->get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

// Disable this for analyzer since this contains use-after-free and double-free
// error.
#ifndef __clang_analyzer__
TEST(ObjectGetInfoTest, ClosedValidHandleFails) {
  zx::event event;

  // Create a valid event.
  ASSERT_OK(zx::event::create(0u, &event));
  ASSERT_OK(event.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));

  // Close and verify is no longer ok.
  event.reset(event.get());

  ASSERT_NOT_OK(event.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}
#endif

// We create an event and check that ZX_INFO_HANDLE_COUNT stats at 1 and
// goes up for each new handle minted from it and goes down for each handle
// closed.
TEST(ObjectGetInfoTest, HandleCountCorrectness) {
  zx::event event, dup1, dup2, dup3;

  EXPECT_EQ(GetHandleCountOrZero(event), 0u);

  ASSERT_OK(zx::event::create(0u, &event));
  EXPECT_EQ(GetHandleCountOrZero(event), 1u);

  ASSERT_OK(event.duplicate(ZX_RIGHT_SIGNAL, &dup1));
  EXPECT_EQ(GetHandleCountOrZero(event), 2u);

  ASSERT_OK(event.duplicate(ZX_RIGHT_SIGNAL, &dup2));
  EXPECT_EQ(GetHandleCountOrZero(event), 3u);

  ASSERT_OK(event.duplicate(ZX_RIGHT_SIGNAL, &dup3));
  EXPECT_EQ(GetHandleCountOrZero(event), 4u);

  dup3.reset();
  EXPECT_EQ(GetHandleCountOrZero(event), 3u);

  dup2.reset();
  EXPECT_EQ(GetHandleCountOrZero(event), 2u);

  dup1.reset();
  EXPECT_EQ(GetHandleCountOrZero(event), 1u);

  event.reset();
  EXPECT_EQ(GetHandleCountOrZero(event), 0u);
}

constexpr auto provider = []() { return zx::handle(); };

TEST(ObjectGetInfoTest, InvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES((CheckInvalidHandleFails<void*>(ZX_INFO_HANDLE_VALID, 1, provider)));
}

// As reference from previous object-info test.
// TODO(dbort): Test resource topics
// RUN_MULTI_ENTRY_TESTS(ZX_INFO_CPU_STATS, zx_info_cpu_stats_t, get_root_resource);
// RUN_SINGLE_ENTRY_TESTS(ZX_INFO_KMEM_STATS, zx_info_kmem_stats_t, get_root_resource);

}  // namespace
}  // namespace object_info_test

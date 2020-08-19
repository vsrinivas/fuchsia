// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <lib/zx/event.h>
#include <lib/zx/thread.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kEventOption = 0u;
constexpr uint32_t kSocketOption = 0u;

TEST(HandleInfoTest, DupAndInfoRights) {
  zx::event orig_event;
  ASSERT_OK(zx::event::create(kEventOption, &orig_event));
  zx::event duped_event;
  ASSERT_OK(orig_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &duped_event));

  ASSERT_OK(
      orig_event.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0u /*buffer_size*/, nullptr, nullptr));
  orig_event.reset();
  ASSERT_EQ(
      orig_event.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0u /*buffer_size*/, nullptr, nullptr),
      ZX_ERR_BAD_HANDLE);

  zx_info_handle_basic_t info = {};
  ASSERT_EQ(duped_event.get_info(ZX_INFO_HANDLE_BASIC, &info, 4u /*buffer_size*/, nullptr, nullptr),
            ZX_ERR_BUFFER_TOO_SMALL);

  ASSERT_OK(duped_event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  constexpr zx_rights_t evr = ZX_RIGHTS_BASIC | ZX_RIGHT_SIGNAL;

  EXPECT_GT(info.koid, 0ULL, "object id should be positive");
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT, "handle should be an event");
  EXPECT_EQ(info.rights, evr, "wrong set of rights");
  EXPECT_EQ(info.related_koid, 0ULL, "events don't have associated koid");
}

TEST(HandleInfoTest, RelatedKoid) {
  zx_info_handle_basic_t info_job = {};
  zx_info_handle_basic_t info_process = {};

  ASSERT_OK(zx::job::default_job()->get_info(ZX_INFO_HANDLE_BASIC, &info_job, sizeof(info_job),
                                             nullptr, nullptr));

  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_HANDLE_BASIC, &info_process, sizeof(info_process),
                                          nullptr, nullptr));

  EXPECT_EQ(info_job.type, ZX_OBJ_TYPE_JOB);
  EXPECT_EQ(info_process.type, ZX_OBJ_TYPE_PROCESS);

  zx::thread thread;

  ASSERT_OK(zx::thread::create(*zx::process::self(), "hitr", 4, 0u, &thread));

  zx_info_handle_basic_t info_thread = {};

  ASSERT_OK(
      thread.get_info(ZX_INFO_HANDLE_BASIC, &info_thread, sizeof(info_thread), nullptr, nullptr));

  EXPECT_EQ(info_thread.type, ZX_OBJ_TYPE_THREAD);

  // The related koid of a process is its job and this test assumes that the
  // default job is in fact the parent job of this test. Equivalently, a thread's
  // associated koid is the process koid.
  EXPECT_EQ(info_process.related_koid, info_job.koid);
  EXPECT_EQ(info_thread.related_koid, info_process.koid);

  thread.reset();

  zx::socket socket_local, socket_remote;
  ASSERT_OK(zx::socket::create(kSocketOption, &socket_local, &socket_remote));

  zx_info_handle_basic_t info_socket_local = {};
  ASSERT_OK(socket_local.get_info(ZX_INFO_HANDLE_BASIC, &info_socket_local,
                                  sizeof(info_socket_local), nullptr, nullptr));

  zx_info_handle_basic_t info_socket_remote = {};
  ASSERT_OK(socket_remote.get_info(ZX_INFO_HANDLE_BASIC, &info_socket_remote,
                                   sizeof(info_socket_remote), nullptr, nullptr));

  EXPECT_EQ(info_socket_local.type, ZX_OBJ_TYPE_SOCKET);
  EXPECT_EQ(info_socket_remote.type, ZX_OBJ_TYPE_SOCKET);

  // The related koid of a socket pair are each other koids.
  EXPECT_EQ(info_socket_local.related_koid, info_socket_remote.koid);
  EXPECT_EQ(info_socket_remote.related_koid, info_socket_local.koid);
}

TEST(HandleInfoTest, DuplicateRights) {
  zx::event orig_event;
  ASSERT_OK(zx::event::create(kEventOption, &orig_event));
  zx::event duped_ro1_event, duped_ro2_event;
  ASSERT_OK(orig_event.duplicate(ZX_RIGHT_WAIT, &duped_ro1_event));
  ASSERT_OK(orig_event.duplicate(ZX_RIGHT_WAIT, &duped_ro2_event));

  zx_info_handle_basic_t info = {};
  ASSERT_OK(duped_ro1_event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  EXPECT_EQ(info.rights, ZX_RIGHT_WAIT, "wrong set of rights");

  zx::event duped_ro3_event;
  // Duplicate right removed, so this fails.
  ASSERT_EQ(duped_ro1_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &duped_ro3_event),
            ZX_ERR_ACCESS_DENIED);

  ASSERT_EQ(orig_event.duplicate(ZX_RIGHT_EXECUTE | ZX_RIGHT_WAIT, &duped_ro3_event),
            ZX_ERR_INVALID_ARGS, "invalid right");

  EXPECT_TRUE(orig_event.is_valid(), "original handle should be valid");
  EXPECT_TRUE(duped_ro1_event.is_valid(), "duped handle should be valid");
  EXPECT_TRUE(duped_ro2_event.is_valid(), "duped handle should be valid");
  EXPECT_FALSE(duped_ro3_event.is_valid(), "duped handle should be invalid");
}

TEST(HandleInfoTest, ReplaceRights) {
  zx::event event1, event2;
  ASSERT_OK(zx::event::create(kEventOption, &event1));
  ASSERT_OK(event1.replace(ZX_RIGHT_WAIT, &event2));

  zx_info_handle_basic_t info = {};
  ASSERT_OK(event2.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.rights, ZX_RIGHT_WAIT, "wrong set of rights");

  EXPECT_FALSE(event1.is_valid(), "replaced event should be invalid");

  EXPECT_EQ(event2.replace(ZX_RIGHT_SIGNAL | ZX_RIGHT_WAIT, &event1), ZX_ERR_INVALID_ARGS,
            "cannot upgrade rights");
  // event2 should also now be invalid.
  EXPECT_FALSE(event2.is_valid(), "replaced event should be invalid on failure");
}

}  // namespace

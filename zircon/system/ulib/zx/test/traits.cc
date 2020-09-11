// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/time.h>
#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/exception.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <lib/zx/vmar.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>

#include <zxtest/zxtest.h>

#include "util.h"

template <typename Handle>
void Duplicating(const Handle& handle) {
  zx_status_t expected_status = ZX_OK;
  if (!zx::object_traits<Handle>::supports_duplication) {
    expected_status = ZX_ERR_ACCESS_DENIED;
  }

  zx_handle_t copy = ZX_HANDLE_INVALID;
  zx_status_t status = zx_handle_duplicate(handle.get(), ZX_RIGHT_SAME_RIGHTS, &copy);
  if (copy != ZX_HANDLE_INVALID) {
    zx_handle_close(copy);
  }

  ASSERT_STATUS(status, expected_status);
}

template <typename Handle>
void GetChild(const Handle& handle) {
  // object_get_child looks up handles by koid so it's tricky to both make this generic and also
  // have the call succeed (with ZX_OK), so we just look for NOT_FOUND vs WRONG_TYPE.
  zx_status_t expected_status = ZX_ERR_NOT_FOUND;
  if (!zx::object_traits<Handle>::supports_get_child) {
    // This is ACCESS_DENIED instead of WRONG_TYPE because unsupported types also lack the ENUMERATE
    // right.
    expected_status = ZX_ERR_ACCESS_DENIED;
  }

  zx_handle_t child = ZX_HANDLE_INVALID;
  zx_status_t status =
      zx_object_get_child(handle.get(), ZX_KOID_FIRST, ZX_RIGHT_SAME_RIGHTS, &child);
  if (child != ZX_HANDLE_INVALID) {
    zx_handle_close(child);
  }

  ASSERT_STATUS(status, expected_status);
}

template <typename Handle>
void SetProfile(const Handle& handle) {
  zx_status_t expected_status = ZX_OK;
  if (!zx::object_traits<Handle>::supports_set_profile) {
    expected_status = ZX_ERR_WRONG_TYPE;
  }

  zx::profile profile;
  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
  info.priority = ZX_PRIORITY_LOWEST;
  ASSERT_OK(zx::profile::create(GetRootJob(), 0u, &info, &profile));

  zx_status_t status = zx_object_set_profile(handle.get(), profile.get(), 0u);

  ASSERT_STATUS(status, expected_status);
}

template <typename Handle>
void UserSignaling(const Handle& handle) {
  zx_status_t expected_status = ZX_OK;
  if (!zx::object_traits<Handle>::supports_user_signal) {
    expected_status = ZX_ERR_ACCESS_DENIED;
  }

  zx_handle_t copy = ZX_HANDLE_INVALID;
  zx_status_t status = zx_object_signal(handle.get(), 0u, ZX_USER_SIGNAL_0);
  if (copy != ZX_HANDLE_INVALID) {
    zx_handle_close(copy);
  }

  ASSERT_STATUS(status, expected_status);
}

template <typename Handle>
void Waiting(const Handle& handle) {
  zx_status_t expected_status = ZX_OK;
  if (!zx::object_traits<Handle>::supports_wait) {
    expected_status = ZX_ERR_ACCESS_DENIED;
  }

  zx_handle_t copy = ZX_HANDLE_INVALID;
  zx_status_t status = zx_object_wait_one(handle.get(), ZX_USER_SIGNAL_0, 0u, nullptr);
  if (copy != ZX_HANDLE_INVALID) {
    zx_handle_close(copy);
  }

  ASSERT_STATUS(status, expected_status);
}

template <typename Handle>
void Peering(const Handle& handle) {
  zx_status_t expected_status = ZX_OK;
  if (!zx::object_traits<Handle>::has_peer_handle) {
    expected_status = ZX_ERR_ACCESS_DENIED;
  }

  zx_status_t status = zx_object_signal_peer(handle.get(), 0u, ZX_USER_SIGNAL_0);

  ASSERT_STATUS(status, expected_status);
}

[[noreturn]] void do_segfault(uintptr_t /*arg1*/, uintptr_t /*arg2*/) {
  volatile int* p = 0;
  *p = 1;
  zx_thread_exit();
}

TEST(TraitsTestCase, EventTraits) {
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));
  ASSERT_NO_FATAL_FAILURES(Duplicating(event));
  ASSERT_NO_FATAL_FAILURES(GetChild(event));
  ASSERT_NO_FATAL_FAILURES(SetProfile(event));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(event));
  ASSERT_NO_FATAL_FAILURES(Waiting(event));
  ASSERT_NO_FATAL_FAILURES(Peering(event));
}

TEST(TraitsTestCase, ThreadTraits) {
  zx::thread thread;
  ASSERT_OK(zx::thread::create(*zx::process::self(), "", 0u, 0u, &thread));
  ASSERT_NO_FATAL_FAILURES(Duplicating(thread));
  ASSERT_NO_FATAL_FAILURES(GetChild(thread));
  ASSERT_NO_FATAL_FAILURES(SetProfile(thread));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(thread));
  ASSERT_NO_FATAL_FAILURES(Waiting(thread));
  ASSERT_NO_FATAL_FAILURES(Peering(thread));
}

TEST(TraitsTestCase, ProcessTraits) {
  zx::process process;
  zx::vmar vmar;
  ASSERT_OK(zx::process::create(*zx::job::default_job(), "", 0u, 0u, &process, &vmar));
  ASSERT_NO_FATAL_FAILURES(Duplicating(process));
  ASSERT_NO_FATAL_FAILURES(GetChild(process));
  ASSERT_NO_FATAL_FAILURES(SetProfile(process));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(process));
  ASSERT_NO_FATAL_FAILURES(Waiting(process));
  ASSERT_NO_FATAL_FAILURES(Peering(process));
}

TEST(TraitsTestCase, JobTraits) {
  zx::job job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0u, &job));
  ASSERT_NO_FATAL_FAILURES(Duplicating(job));
  ASSERT_NO_FATAL_FAILURES(GetChild(job));
  ASSERT_NO_FATAL_FAILURES(SetProfile(job));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(job));
  ASSERT_NO_FATAL_FAILURES(Waiting(job));
  ASSERT_NO_FATAL_FAILURES(Peering(job));
}

TEST(TraitsTestCase, VmoTraits) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096u, 0u, &vmo));
  ASSERT_NO_FATAL_FAILURES(Duplicating(vmo));
  ASSERT_NO_FATAL_FAILURES(GetChild(vmo));
  ASSERT_NO_FATAL_FAILURES(SetProfile(vmo));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(vmo));
  ASSERT_NO_FATAL_FAILURES(Waiting(vmo));
  ASSERT_NO_FATAL_FAILURES(Peering(vmo));
}

TEST(TraitsTestCase, BtiTraits) {
  // Creating a zx::bti is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_TRUE(zx::object_traits<zx::bti>::supports_duplication);
  ASSERT_FALSE(zx::object_traits<zx::bti>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::bti>::supports_set_profile);
  ASSERT_TRUE(zx::object_traits<zx::bti>::supports_user_signal);
  ASSERT_TRUE(zx::object_traits<zx::bti>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::bti>::has_peer_handle);
}

TEST(TraitsTestCase, ResourceTraits) {
  // Creating a zx::resource is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_TRUE(zx::object_traits<zx::resource>::supports_duplication);
  ASSERT_TRUE(zx::object_traits<zx::resource>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::resource>::supports_set_profile);
  ASSERT_TRUE(zx::object_traits<zx::resource>::supports_user_signal);
  ASSERT_TRUE(zx::object_traits<zx::resource>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::resource>::has_peer_handle);
}

TEST(TraitsTestCase, TimerTraits) {
  zx::timer timer;
  ASSERT_OK(zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &timer));
  ASSERT_NO_FATAL_FAILURES(Duplicating(timer));
  ASSERT_NO_FATAL_FAILURES(GetChild(timer));
  ASSERT_NO_FATAL_FAILURES(SetProfile(timer));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(timer));
  ASSERT_NO_FATAL_FAILURES(Waiting(timer));
  ASSERT_NO_FATAL_FAILURES(Peering(timer));
}

TEST(TraitsTestCase, ChannelTraits) {
  zx::channel channel, channel2;
  ASSERT_OK(zx::channel::create(0u, &channel, &channel2));
  ASSERT_NO_FATAL_FAILURES(Duplicating(channel));
  ASSERT_NO_FATAL_FAILURES(GetChild(channel));
  ASSERT_NO_FATAL_FAILURES(SetProfile(channel));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(channel));
  ASSERT_NO_FATAL_FAILURES(Waiting(channel));
  ASSERT_NO_FATAL_FAILURES(Peering(channel));
}

TEST(TraitsTestCase, EventPairTraits) {
  zx::eventpair eventpair, eventpair2;
  ASSERT_OK(zx::eventpair::create(0u, &eventpair, &eventpair2));
  ASSERT_NO_FATAL_FAILURES(Duplicating(eventpair));
  ASSERT_NO_FATAL_FAILURES(GetChild(eventpair));
  ASSERT_NO_FATAL_FAILURES(SetProfile(eventpair));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(eventpair));
  ASSERT_NO_FATAL_FAILURES(Waiting(eventpair));
  ASSERT_NO_FATAL_FAILURES(Peering(eventpair));
}

TEST(TraitsTestCase, FifoTraits) {
  zx::fifo fifo, fifo2;
  ASSERT_OK(zx::fifo::create(16u, 16u, 0u, &fifo, &fifo2));
  ASSERT_NO_FATAL_FAILURES(Duplicating(fifo));
  ASSERT_NO_FATAL_FAILURES(GetChild(fifo));
  ASSERT_NO_FATAL_FAILURES(SetProfile(fifo));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(fifo));
  ASSERT_NO_FATAL_FAILURES(Waiting(fifo));
  ASSERT_NO_FATAL_FAILURES(Peering(fifo));
}

TEST(TraitsTestCase, DebugLogTraits) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  constexpr char kWriteOnlyLogPath[] = "/svc/" fuchsia_boot_WriteOnlyLog_Name;
  ASSERT_OK(fdio_service_connect(kWriteOnlyLogPath, remote.release()));
  zx::debuglog debuglog;
  ASSERT_OK(fuchsia_boot_WriteOnlyLogGet(local.get(), debuglog.reset_and_get_address()));

  ASSERT_NO_FATAL_FAILURES(Duplicating(debuglog));
  ASSERT_NO_FATAL_FAILURES(GetChild(debuglog));
  ASSERT_NO_FATAL_FAILURES(SetProfile(debuglog));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(debuglog));
  ASSERT_NO_FATAL_FAILURES(Waiting(debuglog));
  ASSERT_NO_FATAL_FAILURES(Peering(debuglog));
}

TEST(TraitsTestCase, PmtTraits) {
  // Creating a zx::pmt is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_FALSE(zx::object_traits<zx::pmt>::supports_duplication);
  ASSERT_FALSE(zx::object_traits<zx::pmt>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::pmt>::supports_set_profile);
  ASSERT_FALSE(zx::object_traits<zx::pmt>::supports_user_signal);
  ASSERT_FALSE(zx::object_traits<zx::pmt>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::pmt>::has_peer_handle);
}

TEST(TraitsTestCase, SocketTraits) {
  zx::socket socket, socket2;
  ASSERT_OK(zx::socket::create(0u, &socket, &socket2));
  ASSERT_NO_FATAL_FAILURES(Duplicating(socket));
  ASSERT_NO_FATAL_FAILURES(GetChild(socket));
  ASSERT_NO_FATAL_FAILURES(SetProfile(socket));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(socket));
  ASSERT_NO_FATAL_FAILURES(Waiting(socket));
  ASSERT_NO_FATAL_FAILURES(Peering(socket));
}

TEST(TraitsTestCase, PortTraits) {
  zx::port port;
  ASSERT_OK(zx::port::create(0u, &port));
  ASSERT_NO_FATAL_FAILURES(Duplicating(port));
  ASSERT_NO_FATAL_FAILURES(GetChild(port));
  ASSERT_NO_FATAL_FAILURES(SetProfile(port));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(port));
  ASSERT_NO_FATAL_FAILURES(Waiting(port));
  ASSERT_NO_FATAL_FAILURES(Peering(port));
}

TEST(TraitsTestCase, VmarTraits) {
  zx::vmar vmar;
  uintptr_t addr;
  ASSERT_OK(zx::vmar::root_self()->allocate2(0u, 0u, 4096u, &vmar, &addr));
  ASSERT_NO_FATAL_FAILURES(Duplicating(vmar));
  ASSERT_NO_FATAL_FAILURES(GetChild(vmar));
  ASSERT_NO_FATAL_FAILURES(SetProfile(vmar));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(vmar));
  ASSERT_NO_FATAL_FAILURES(Waiting(vmar));
  ASSERT_NO_FATAL_FAILURES(Peering(vmar));
}

TEST(TraitsTestCase, InterruptTraits) {
  // Creating a zx::interrupt is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_TRUE(zx::object_traits<zx::interrupt>::supports_duplication);
  ASSERT_FALSE(zx::object_traits<zx::interrupt>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::interrupt>::supports_set_profile);
  ASSERT_FALSE(zx::object_traits<zx::interrupt>::supports_user_signal);
  ASSERT_TRUE(zx::object_traits<zx::interrupt>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::interrupt>::has_peer_handle);
}

TEST(TraitsTestCase, GuestTraits) {
  // Creating a zx::guest is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_TRUE(zx::object_traits<zx::guest>::supports_duplication);
  ASSERT_FALSE(zx::object_traits<zx::guest>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::guest>::supports_set_profile);
  ASSERT_FALSE(zx::object_traits<zx::guest>::supports_user_signal);
  ASSERT_FALSE(zx::object_traits<zx::guest>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::guest>::has_peer_handle);
}

TEST(TraitsTestCase, IommuTraits) {
  // Creating a zx::iommu is too hard in a generic testing
  // environment. Instead, we just assert it's got the traits we
  // want.
  ASSERT_TRUE(zx::object_traits<zx::iommu>::supports_duplication);
  ASSERT_FALSE(zx::object_traits<zx::iommu>::supports_get_child);
  ASSERT_FALSE(zx::object_traits<zx::iommu>::supports_set_profile);
  ASSERT_TRUE(zx::object_traits<zx::iommu>::supports_user_signal);
  ASSERT_TRUE(zx::object_traits<zx::iommu>::supports_wait);
  ASSERT_FALSE(zx::object_traits<zx::iommu>::has_peer_handle);
}

TEST(TraitsTestCase, ExceptionTraits) {
  // Create a thread that segfaults so we can catch and analyze the
  // resulting exception object.
  alignas(16) static uint8_t thread_stack[1024];
  zx::thread thread;
  zx::channel exception_channel;
  ASSERT_OK(zx::thread::create(*zx::process::self(), "", 0, 0, &thread));
  ASSERT_OK(thread.create_exception_channel(0, &exception_channel));

  // Stack grows down, make sure to pass a pointer to the end.
  ASSERT_OK(thread.start(&do_segfault, thread_stack + sizeof(thread_stack), 0, 0));

  zx::exception exception;
  zx_exception_info_t info;
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr));

  ASSERT_NO_FATAL_FAILURES(Duplicating(exception));
  ASSERT_NO_FATAL_FAILURES(GetChild(exception));
  ASSERT_NO_FATAL_FAILURES(SetProfile(exception));
  ASSERT_NO_FATAL_FAILURES(UserSignaling(exception));
  ASSERT_NO_FATAL_FAILURES(Waiting(exception));
  ASSERT_NO_FATAL_FAILURES(Peering(exception));

  ASSERT_OK(test_exceptions::ExitExceptionZxThread(std::move(exception)));
  ASSERT_OK(thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/type_support.h>
#include <lib/fzl/time.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/job.h>
#include <lib/zx/log.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <lib/zx/vmar.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

template <typename Handle>
bool duplicating(const Handle& handle) {
    BEGIN_TEST;

    zx_status_t expected_status = ZX_OK;
    if (!zx::object_traits<Handle>::supports_duplication) {
        expected_status = ZX_ERR_ACCESS_DENIED;
    }

    zx_handle_t copy = ZX_HANDLE_INVALID;
    zx_status_t status = zx_handle_duplicate(handle.get(), ZX_RIGHT_SAME_RIGHTS, &copy);
    if (copy != ZX_HANDLE_INVALID) {
        zx_handle_close(copy);
    }

    ASSERT_EQ(status, expected_status);

    END_TEST;
}

template <typename Handle>
bool user_signaling(const Handle& handle) {
    BEGIN_TEST;

    zx_status_t expected_status = ZX_OK;
    if (!zx::object_traits<Handle>::supports_user_signal) {
        expected_status = ZX_ERR_ACCESS_DENIED;
    }

    zx_handle_t copy = ZX_HANDLE_INVALID;
    zx_status_t status = zx_object_signal(handle.get(), 0u, ZX_USER_SIGNAL_0);
    if (copy != ZX_HANDLE_INVALID) {
        zx_handle_close(copy);
    }

    ASSERT_EQ(status, expected_status);

    END_TEST;
}

template <typename Handle>
bool waiting(const Handle& handle) {
    BEGIN_TEST;

    zx_status_t expected_status = ZX_OK;
    if (!zx::object_traits<Handle>::supports_wait) {
        expected_status = ZX_ERR_ACCESS_DENIED;
    }

    zx_handle_t copy = ZX_HANDLE_INVALID;
    zx_status_t status = zx_object_wait_one(handle.get(), ZX_USER_SIGNAL_0, 0u, nullptr);
    if (copy != ZX_HANDLE_INVALID) {
        zx_handle_close(copy);
    }

    ASSERT_EQ(status, expected_status);

    END_TEST;
}

template <typename Handle>
bool peering(const Handle& handle) {
    BEGIN_TEST;

    zx_status_t expected_status = ZX_OK;
    if (!zx::object_traits<Handle>::has_peer_handle) {
        expected_status = ZX_ERR_ACCESS_DENIED;
    }

    zx_status_t status = zx_object_signal_peer(handle.get(), 0u, ZX_USER_SIGNAL_0);

    ASSERT_EQ(status, expected_status);

    END_TEST;
}

bool traits_test() {
    BEGIN_TEST;

    {
        zx::event event;
        ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
        duplicating(event);
        user_signaling(event);
        waiting(event);
        peering(event);
    }

    {
        zx::thread thread;
        ASSERT_EQ(zx::thread::create(*zx::process::self(), "", 0u, 0u, &thread), ZX_OK);
        duplicating(thread);
        user_signaling(thread);
        waiting(thread);
        peering(thread);
    }

    {
        zx::process process;
        zx::vmar vmar;
        ASSERT_EQ(zx::process::create(*zx::job::default_job(), "", 0u, 0u, &process, &vmar), ZX_OK);
        duplicating(process);
        user_signaling(process);
        waiting(process);
        peering(process);
    }

    {
        zx::job job;
        ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0u, &job), ZX_OK);
        duplicating(job);
        user_signaling(job);
        waiting(job);
        peering(job);
    }

    {
        zx::vmo vmo;
        ASSERT_EQ(zx::vmo::create(4096u, 0u, &vmo), ZX_OK);
        duplicating(vmo);
        user_signaling(vmo);
        waiting(vmo);
        peering(vmo);
    }

    {
        // Creating a zx::bti is too hard in a generic testing
        // environment. Instead, we just assert it's got the traits we
        // want.
        ASSERT_EQ(zx::object_traits<zx::bti>::supports_duplication, true);
        ASSERT_EQ(zx::object_traits<zx::bti>::supports_user_signal, true);
        ASSERT_EQ(zx::object_traits<zx::bti>::supports_wait, true);
        ASSERT_EQ(zx::object_traits<zx::bti>::has_peer_handle, false);
    }

    {
        // Creating a zx::resource is too hard in a generic testing
        // environment. Instead, we just assert it's got the traits we
        // want.
        ASSERT_EQ(zx::object_traits<zx::resource>::supports_duplication, true);
        ASSERT_EQ(zx::object_traits<zx::resource>::supports_user_signal, true);
        ASSERT_EQ(zx::object_traits<zx::resource>::supports_wait, true);
        ASSERT_EQ(zx::object_traits<zx::resource>::has_peer_handle, false);
    }

    {
        zx::timer timer;
        ASSERT_EQ(zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &timer), ZX_OK);
        duplicating(timer);
        user_signaling(timer);
        waiting(timer);
        peering(timer);
    }

    {
        zx::channel channel, channel2;
        ASSERT_EQ(zx::channel::create(0u, &channel, &channel2), ZX_OK);
        duplicating(channel);
        user_signaling(channel);
        waiting(channel);
        peering(channel);
    }

    {
        zx::eventpair eventpair, eventpair2;
        ASSERT_EQ(zx::eventpair::create(0u, &eventpair, &eventpair2), ZX_OK);
        duplicating(eventpair);
        user_signaling(eventpair);
        waiting(eventpair);
        peering(eventpair);
    }

    {
        zx::fifo fifo, fifo2;
        ASSERT_EQ(zx::fifo::create(16u, 16u, 0u, &fifo, &fifo2), ZX_OK);
        duplicating(fifo);
        user_signaling(fifo);
        waiting(fifo);
        peering(fifo);
    }

    {
        zx::log log;
        ASSERT_EQ(zx::log::create(0u, &log), ZX_OK);
        duplicating(log);
        user_signaling(log);
        waiting(log);
        peering(log);
    }

    {
        // Creating a zx::pmt is too hard in a generic testing
        // environment. Instead, we just assert it's got the traits we
        // want.
        ASSERT_EQ(zx::object_traits<zx::pmt>::supports_duplication, false);
        ASSERT_EQ(zx::object_traits<zx::pmt>::supports_user_signal, false);
        ASSERT_EQ(zx::object_traits<zx::pmt>::supports_wait, false);
        ASSERT_EQ(zx::object_traits<zx::pmt>::has_peer_handle, false);
    }

    {
        zx::socket socket, socket2;
        ASSERT_EQ(zx::socket::create(0u, &socket, &socket2), ZX_OK);
        duplicating(socket);
        user_signaling(socket);
        waiting(socket);
        peering(socket);
    }

    {
        zx::port port;
        ASSERT_EQ(zx::port::create(0u, &port), ZX_OK);
        duplicating(port);
        user_signaling(port);
        waiting(port);
        peering(port);
    }

    {
        zx::vmar vmar;
        uintptr_t addr;
        ASSERT_EQ(zx::vmar::root_self()->allocate(0u, 4096u, 0u, &vmar, &addr), ZX_OK);
        duplicating(vmar);
        user_signaling(vmar);
        waiting(vmar);
        peering(vmar);
    }

    {
        // Creating a zx::interrupt is too hard in a generic testing
        // environment. Instead, we just assert it's got the traits we
        // want.
        ASSERT_EQ(zx::object_traits<zx::interrupt>::supports_duplication, false);
        ASSERT_EQ(zx::object_traits<zx::interrupt>::supports_user_signal, false);
        ASSERT_EQ(zx::object_traits<zx::interrupt>::supports_wait, true);
        ASSERT_EQ(zx::object_traits<zx::interrupt>::has_peer_handle, false);
    }

    {
        // Creating a zx::guest is too hard in a generic testing
        // environment. Instead, we just assert it's got the traits we
        // want.
        ASSERT_EQ(zx::object_traits<zx::guest>::supports_duplication, true);
        ASSERT_EQ(zx::object_traits<zx::guest>::supports_user_signal, false);
        ASSERT_EQ(zx::object_traits<zx::guest>::supports_wait, false);
        ASSERT_EQ(zx::object_traits<zx::guest>::has_peer_handle, false);
    }

    END_TEST;
}

BEGIN_TEST_CASE(libzx_traits_tests)

RUN_TEST(traits_test)

END_TEST_CASE(libzx_traits_tests)

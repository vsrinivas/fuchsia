// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

#include <zx/channel.h>
#include <zx/event.h>
#include <zx/eventpair.h>
#include <zx/handle.h>
#include <zx/job.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <zx/thread.h>
#include <zx/time.h>
#include <zx/vmar.h>

#include <fbl/type_support.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <unistd.h>
#include <unittest/unittest.h>

static zx_status_t validate_handle(zx_handle_t handle) {
    return zx_object_get_info(handle, ZX_INFO_HANDLE_VALID,
                              nullptr, 0, 0u, nullptr);
}

static bool handle_invalid_test() {
    BEGIN_TEST;
    zx::handle handle;
    // A default constructed handle is invalid.
    ASSERT_EQ(handle.release(), ZX_HANDLE_INVALID);
    END_TEST;
}

static bool handle_close_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    ASSERT_EQ(validate_handle(raw_event), ZX_OK);
    {
        zx::handle handle(raw_event);
    }
    // Make sure the handle was closed.
    ASSERT_EQ(validate_handle(raw_event), ZX_ERR_BAD_HANDLE);
    END_TEST;
}

static bool handle_move_test() {
    BEGIN_TEST;
    zx::event event;
    // Check move semantics.
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    zx::handle handle(fbl::move(event));
    ASSERT_EQ(event.release(), ZX_HANDLE_INVALID);
    ASSERT_EQ(validate_handle(handle.get()), ZX_OK);
    END_TEST;
}

static bool handle_duplicate_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    zx::handle dup;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    zx::handle handle(raw_event);
    ASSERT_EQ(handle.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), ZX_OK);
    ASSERT_EQ(validate_handle(raw_event), ZX_OK);
    END_TEST;
}

static bool handle_replace_test() {
    BEGIN_TEST;
    zx_handle_t raw_event;
    zx::handle rep;
    ASSERT_EQ(zx_event_create(0u, &raw_event), ZX_OK);
    {
        zx::handle handle(raw_event);
        ASSERT_EQ(handle.replace(ZX_RIGHT_SAME_RIGHTS, &rep), ZX_OK);
        ASSERT_EQ(handle.release(), ZX_HANDLE_INVALID);
    }
    // The original shoould be invalid and the replacement should be valid.
    ASSERT_EQ(validate_handle(raw_event), ZX_ERR_BAD_HANDLE);
    ASSERT_EQ(validate_handle(rep.get()), ZX_OK);
    END_TEST;
}

static bool event_test() {
    BEGIN_TEST;
    zx::event event;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    ASSERT_EQ(validate_handle(event.get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool event_duplicate_test() {
    BEGIN_TEST;
    zx::event event;
    zx::event dup;
    ASSERT_EQ(zx::event::create(0u, &event), ZX_OK);
    ASSERT_EQ(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), ZX_OK);
    ASSERT_EQ(validate_handle(event.get()), ZX_OK);
    END_TEST;
}

static bool channel_test() {
    BEGIN_TEST;
    zx::channel channel[2];
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);
    ASSERT_EQ(validate_handle(channel[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(channel[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool socket_test() {
    BEGIN_TEST;
    zx::socket socket[2];
    ASSERT_EQ(zx::socket::create(0u, &socket[0], &socket[1]), ZX_OK);
    ASSERT_EQ(validate_handle(socket[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(socket[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool eventpair_test() {
    BEGIN_TEST;
    zx::eventpair evpair[2];
    ASSERT_EQ(zx::eventpair::create(0u, &evpair[0], &evpair[1]), ZX_OK);
    ASSERT_EQ(validate_handle(evpair[0].get()), ZX_OK);
    ASSERT_EQ(validate_handle(evpair[1].get()), ZX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool vmar_test() {
    BEGIN_TEST;
    zx::vmar vmar;
    const size_t size = getpagesize();
    uintptr_t addr;
    ASSERT_EQ(zx::vmar::root_self().allocate(0u, size, ZX_VM_FLAG_CAN_MAP_READ, &vmar, &addr),
              ZX_OK);
    ASSERT_EQ(validate_handle(vmar.get()), ZX_OK);
    ASSERT_EQ(vmar.destroy(), ZX_OK);
    // TODO(teisenbe): test more.
    END_TEST;
}

static bool port_test() {
    BEGIN_TEST;
    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);
    ASSERT_EQ(validate_handle(port.get()), ZX_OK);

    zx::channel channel[2];
    auto key = 1111ull;
    ASSERT_EQ(zx::channel::create(0u, &channel[0], &channel[1]), ZX_OK);
    ASSERT_EQ(channel[0].wait_async(
        port, key, ZX_CHANNEL_READABLE, ZX_WAIT_ASYNC_ONCE), ZX_OK);
    ASSERT_EQ(channel[1].write(0u, "12345", 5, nullptr, 0u), ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(port.wait(zx::time(), &packet, 0u), ZX_OK);
    ASSERT_EQ(packet.key, key);
    ASSERT_EQ(packet.type, ZX_PKT_TYPE_SIGNAL_ONE);
    ASSERT_EQ(packet.signal.count, 1u);
    END_TEST;
}

static bool time_test() {
    BEGIN_TEST;

    ASSERT_EQ(zx::time().get(), 0);
    ASSERT_EQ(zx::time::infinite().get(), ZX_TIME_INFINITE);

    ASSERT_EQ(zx::duration().get(), 0);
    ASSERT_EQ(zx::duration::infinite().get(), ZX_TIME_INFINITE);

    ASSERT_EQ(zx::usec(10).get(), ZX_USEC(10));
    ASSERT_EQ(zx::msec(10).get(), ZX_MSEC(10));
    ASSERT_EQ(zx::sec(10).get(), ZX_SEC(10));
    ASSERT_EQ(zx::min(10).get(), ZX_MIN(10));
    ASSERT_EQ(zx::hour(10).get(), ZX_HOUR(10));

    ASSERT_EQ((zx::time() + zx::usec(19)).get(), ZX_USEC(19));
    ASSERT_EQ((zx::time::infinite() - zx::time()).get(), ZX_TIME_INFINITE);
    ASSERT_EQ((zx::time::infinite() - zx::time::infinite()).get(), 0);
    ASSERT_EQ((zx::time() + zx::duration::infinite()).get(), ZX_TIME_INFINITE);

    // Just a smoke test
    ASSERT_GE(zx::deadline_after(zx::usec(10)).get(), ZX_USEC(10));

    END_TEST;
}

template <typename T>
static bool reference_thing(const T& p) {
    BEGIN_HELPER;
    ASSERT_TRUE(static_cast<bool>(p), "invalid handle");
    END_HELPER;
}

static bool thread_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_thread_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::thread>(zx::thread::self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::thread self = zx::thread::self();

    END_TEST;
}

static bool process_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_process_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::process>(zx::process::self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::process self = zx::process::self();

    END_TEST;
}

static bool vmar_root_self_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_vmar_root_self();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::vmar>(zx::vmar::root_self()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::vmar root_self = zx::vmar::root_self();

    END_TEST;
}

static bool job_default_test() {
    BEGIN_TEST;

    zx_handle_t raw = zx_job_default();
    ASSERT_EQ(validate_handle(raw), ZX_OK);

    EXPECT_TRUE(reference_thing<zx::job>(zx::job::default_job()));
    EXPECT_EQ(validate_handle(raw), ZX_OK);

    // This does not compile:
    //const zx::job default_job = zx::job::default_job();

    END_TEST;
}

BEGIN_TEST_CASE(libmx_tests)
RUN_TEST(handle_invalid_test)
RUN_TEST(handle_close_test)
RUN_TEST(handle_move_test)
RUN_TEST(handle_duplicate_test)
RUN_TEST(handle_replace_test)
RUN_TEST(event_test)
RUN_TEST(event_duplicate_test)
RUN_TEST(channel_test)
RUN_TEST(socket_test)
RUN_TEST(eventpair_test)
RUN_TEST(vmar_test)
RUN_TEST(port_test)
RUN_TEST(time_test)
RUN_TEST(thread_self_test)
RUN_TEST(process_self_test)
RUN_TEST(vmar_root_self_test)
RUN_TEST(job_default_test)
END_TEST_CASE(libmx_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

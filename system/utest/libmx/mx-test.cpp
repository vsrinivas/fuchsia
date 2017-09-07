// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/eventpair.h>
#include <mx/handle.h>
#include <mx/job.h>
#include <mx/port.h>
#include <mx/process.h>
#include <mx/socket.h>
#include <mx/thread.h>
#include <mx/time.h>
#include <mx/vmar.h>

#include <fbl/type_support.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>

#include <unistd.h>
#include <unittest/unittest.h>

static mx_status_t validate_handle(mx_handle_t handle) {
    return mx_object_get_info(handle, MX_INFO_HANDLE_VALID, 0, NULL, 0u, NULL);
}

static bool handle_invalid_test() {
    BEGIN_TEST;
    mx::handle handle;
    // A default constructed handle is invalid.
    ASSERT_EQ(handle.release(), MX_HANDLE_INVALID);
    END_TEST;
}

static bool handle_close_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    ASSERT_EQ(mx_event_create(0u, &raw_event), MX_OK);
    ASSERT_EQ(validate_handle(raw_event), MX_OK);
    {
        mx::handle handle(raw_event);
    }
    // Make sure the handle was closed.
    ASSERT_EQ(validate_handle(raw_event), MX_ERR_BAD_HANDLE);
    END_TEST;
}

static bool handle_move_test() {
    BEGIN_TEST;
    mx::event event;
    // Check move semantics.
    ASSERT_EQ(mx::event::create(0u, &event), MX_OK);
    mx::handle handle(fbl::move(event));
    ASSERT_EQ(event.release(), MX_HANDLE_INVALID);
    ASSERT_EQ(validate_handle(handle.get()), MX_OK);
    END_TEST;
}

static bool handle_duplicate_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    mx::handle dup;
    ASSERT_EQ(mx_event_create(0u, &raw_event), MX_OK);
    mx::handle handle(raw_event);
    ASSERT_EQ(handle.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), MX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), MX_OK);
    ASSERT_EQ(validate_handle(raw_event), MX_OK);
    END_TEST;
}

static bool handle_replace_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    mx::handle rep;
    ASSERT_EQ(mx_event_create(0u, &raw_event), MX_OK);
    {
        mx::handle handle(raw_event);
        ASSERT_EQ(handle.replace(MX_RIGHT_SAME_RIGHTS, &rep), MX_OK);
        ASSERT_EQ(handle.release(), MX_HANDLE_INVALID);
    }
    // The original shoould be invalid and the replacement should be valid.
    ASSERT_EQ(validate_handle(raw_event), MX_ERR_BAD_HANDLE);
    ASSERT_EQ(validate_handle(rep.get()), MX_OK);
    END_TEST;
}

static bool event_test() {
    BEGIN_TEST;
    mx::event event;
    ASSERT_EQ(mx::event::create(0u, &event), MX_OK);
    ASSERT_EQ(validate_handle(event.get()), MX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool event_duplicate_test() {
    BEGIN_TEST;
    mx::event event;
    mx::event dup;
    ASSERT_EQ(mx::event::create(0u, &event), MX_OK);
    ASSERT_EQ(event.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), MX_OK);
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), MX_OK);
    ASSERT_EQ(validate_handle(event.get()), MX_OK);
    END_TEST;
}

static bool channel_test() {
    BEGIN_TEST;
    mx::channel channel[2];
    ASSERT_EQ(mx::channel::create(0u, &channel[0], &channel[1]), MX_OK);
    ASSERT_EQ(validate_handle(channel[0].get()), MX_OK);
    ASSERT_EQ(validate_handle(channel[1].get()), MX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool socket_test() {
    BEGIN_TEST;
    mx::socket socket[2];
    ASSERT_EQ(mx::socket::create(0u, &socket[0], &socket[1]), MX_OK);
    ASSERT_EQ(validate_handle(socket[0].get()), MX_OK);
    ASSERT_EQ(validate_handle(socket[1].get()), MX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool eventpair_test() {
    BEGIN_TEST;
    mx::eventpair evpair[2];
    ASSERT_EQ(mx::eventpair::create(0u, &evpair[0], &evpair[1]), MX_OK);
    ASSERT_EQ(validate_handle(evpair[0].get()), MX_OK);
    ASSERT_EQ(validate_handle(evpair[1].get()), MX_OK);
    // TODO(cpu): test more.
    END_TEST;
}

static bool vmar_test() {
    BEGIN_TEST;
    mx::vmar vmar;
    const size_t size = getpagesize();
    uintptr_t addr;
    ASSERT_EQ(mx::vmar::root_self().allocate(0u, size, MX_VM_FLAG_CAN_MAP_READ, &vmar, &addr),
              MX_OK);
    ASSERT_EQ(validate_handle(vmar.get()), MX_OK);
    ASSERT_EQ(vmar.destroy(), MX_OK);
    // TODO(teisenbe): test more.
    END_TEST;
}

static bool port_test() {
    BEGIN_TEST;
    mx::port port;
    ASSERT_EQ(mx::port::create(0, &port), MX_OK);
    ASSERT_EQ(validate_handle(port.get()), MX_OK);

    mx::channel channel[2];
    auto key = 1111ull;
    ASSERT_EQ(mx::channel::create(0u, &channel[0], &channel[1]), MX_OK);
    ASSERT_EQ(channel[0].wait_async(
        port, key, MX_CHANNEL_READABLE, MX_WAIT_ASYNC_ONCE), MX_OK);
    ASSERT_EQ(channel[1].write(0u, "12345", 5, nullptr, 0u), MX_OK);

    mx_port_packet_t packet = {};
    ASSERT_EQ(port.wait(0ull, &packet, 0u), MX_OK);
    ASSERT_EQ(packet.key, key);
    ASSERT_EQ(packet.type, MX_PKT_TYPE_SIGNAL_ONE);
    ASSERT_EQ(packet.signal.count, 1u);
    END_TEST;
}

static bool time_test() {
    BEGIN_TEST;

    // Just a smoke test
    ASSERT_GE(mx::deadline_after(10), 10);

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

    mx_handle_t raw = mx_thread_self();
    ASSERT_EQ(validate_handle(raw), MX_OK);

    EXPECT_TRUE(reference_thing<mx::thread>(mx::thread::self()));
    EXPECT_EQ(validate_handle(raw), MX_OK);

    // This does not compile:
    //const mx::thread self = mx::thread::self();

    END_TEST;
}

static bool process_self_test() {
    BEGIN_TEST;

    mx_handle_t raw = mx_process_self();
    ASSERT_EQ(validate_handle(raw), MX_OK);

    EXPECT_TRUE(reference_thing<mx::process>(mx::process::self()));
    EXPECT_EQ(validate_handle(raw), MX_OK);

    // This does not compile:
    //const mx::process self = mx::process::self();

    END_TEST;
}

static bool vmar_root_self_test() {
    BEGIN_TEST;

    mx_handle_t raw = mx_vmar_root_self();
    ASSERT_EQ(validate_handle(raw), MX_OK);

    EXPECT_TRUE(reference_thing<mx::vmar>(mx::vmar::root_self()));
    EXPECT_EQ(validate_handle(raw), MX_OK);

    // This does not compile:
    //const mx::vmar root_self = mx::vmar::root_self();

    END_TEST;
}

static bool job_default_test() {
    BEGIN_TEST;

    mx_handle_t raw = mx_job_default();
    ASSERT_EQ(validate_handle(raw), MX_OK);

    EXPECT_TRUE(reference_thing<mx::job>(mx::job::default_job()));
    EXPECT_EQ(validate_handle(raw), MX_OK);

    // This does not compile:
    //const mx::job default_job = mx::job::default_job();

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

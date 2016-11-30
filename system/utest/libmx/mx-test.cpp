// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

#include <mx/channel.h>
#include <mx/event.h>
#include <mx/eventpair.h>
#include <mx/handle.h>
#include <mx/socket.h>
#include <mx/vmar.h>

#include <mxtl/type_support.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <unistd.h>
#include <unittest/unittest.h>

static mx_status_t validate_handle(mx_handle_t handle) {
    return mx_object_get_info(handle, MX_INFO_HANDLE_VALID, 0, NULL, 0u, NULL);
}

static bool handle_invalid_test() {
    BEGIN_TEST;
    mx::handle<void> handle;
    // A default constructed handle is invalid.
    ASSERT_EQ(handle.release(), MX_HANDLE_INVALID, "");
    END_TEST;
}

static bool handle_close_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    ASSERT_EQ(mx_event_create(0u, &raw_event), NO_ERROR, "");
    ASSERT_EQ(validate_handle(raw_event), NO_ERROR, "");
    {
        mx::handle<void> handle(raw_event);
    }
    // Make sure the handle was closed.
    ASSERT_EQ(validate_handle(raw_event), ERR_BAD_HANDLE, "");
    END_TEST;
}

static bool handle_move_test() {
    BEGIN_TEST;
    mx::event event;
    // Check move semantics.
    ASSERT_EQ(mx::event::create(0u, &event), NO_ERROR, "");
    mx::handle<void> handle(mxtl::move(event));
    ASSERT_EQ(event.release(), MX_HANDLE_INVALID, "");
    ASSERT_EQ(validate_handle(handle.get()), NO_ERROR, "");
    END_TEST;
}

static bool handle_duplicate_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    mx::handle<void> dup;
    ASSERT_EQ(mx_event_create(0u, &raw_event), NO_ERROR, "");
    mx::handle<void> handle(raw_event);
    ASSERT_EQ(handle.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), NO_ERROR, "");
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), NO_ERROR, "");
    ASSERT_EQ(validate_handle(raw_event), NO_ERROR, "");
    END_TEST;
}

static bool handle_replace_test() {
    BEGIN_TEST;
    mx_handle_t raw_event;
    mx::handle<void> rep;
    ASSERT_EQ(mx_event_create(0u, &raw_event), NO_ERROR, "");
    {
        mx::handle<void> handle(raw_event);
        ASSERT_EQ(handle.replace(MX_RIGHT_SAME_RIGHTS, &rep), NO_ERROR, "");
        ASSERT_EQ(handle.release(), MX_HANDLE_INVALID, "");
    }
    // The original shoould be invalid and the replacement should be valid.
    ASSERT_EQ(validate_handle(raw_event), ERR_BAD_HANDLE, "");
    ASSERT_EQ(validate_handle(rep.get()), NO_ERROR, "");
    END_TEST;
}

static bool event_test() {
    BEGIN_TEST;
    mx::event event;
    ASSERT_EQ(mx::event::create(0u, &event), NO_ERROR, "");
    ASSERT_EQ(validate_handle(event.get()), NO_ERROR, "");
    // TODO(cpu): test more.
    END_TEST;
}

static bool event_duplicate_test() {
    BEGIN_TEST;
    mx::event event;
    mx::event dup;
    ASSERT_EQ(mx::event::create(0u, &event), NO_ERROR, "");
    ASSERT_EQ(event.duplicate(MX_RIGHT_SAME_RIGHTS, &dup), NO_ERROR, "");
    // The duplicate must be valid as well as the original.
    ASSERT_EQ(validate_handle(dup.get()), NO_ERROR, "");
    ASSERT_EQ(validate_handle(event.get()), NO_ERROR, "");
    END_TEST;
}

static bool channel_test() {
    BEGIN_TEST;
    mx::channel channel[2];
    ASSERT_EQ(mx::channel::create(0u, &channel[0], &channel[1]), NO_ERROR, "");
    ASSERT_EQ(validate_handle(channel[0].get()), NO_ERROR, "");
    ASSERT_EQ(validate_handle(channel[1].get()), NO_ERROR, "");
    // TODO(cpu): test more.
    END_TEST;
}

static bool socket_test() {
    BEGIN_TEST;
    mx::socket socket[2];
    ASSERT_EQ(mx::socket::create(0u, &socket[0], &socket[1]), NO_ERROR, "");
    ASSERT_EQ(validate_handle(socket[0].get()), NO_ERROR, "");
    ASSERT_EQ(validate_handle(socket[1].get()), NO_ERROR, "");
    // TODO(cpu): test more.
    END_TEST;
}

static bool eventpair_test() {
    BEGIN_TEST;
    mx::eventpair evpair[2];
    ASSERT_EQ(mx::eventpair::create(0u, &evpair[0], &evpair[1]), NO_ERROR, "");
    ASSERT_EQ(validate_handle(evpair[0].get()), NO_ERROR, "");
    ASSERT_EQ(validate_handle(evpair[1].get()), NO_ERROR, "");
    // TODO(cpu): test more.
    END_TEST;
}

static bool vmar_test() {
    BEGIN_TEST;
    mx::vmar vmar;
    const size_t size = getpagesize();
    uintptr_t addr;
    ASSERT_EQ(mx::vmar::root_self().allocate(0u, size, MX_VM_FLAG_CAN_MAP_READ, &vmar, &addr),
              NO_ERROR, "");
    ASSERT_EQ(validate_handle(vmar.get()), NO_ERROR, "");
    ASSERT_EQ(vmar.destroy(), NO_ERROR, "");
    // TODO(teisenbe): test more.
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
END_TEST_CASE(libmx_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

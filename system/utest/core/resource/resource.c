// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>
#include <magenta/syscalls/resource.h>
#include <unittest/unittest.h>
#include <stdio.h>

static mx_status_t check_signals(mx_handle_t h, mx_signals_t expected) {
    mx_signals_t observed;
    mx_status_t status = mx_object_wait_one(h, 0u, 0u, &observed);

    // mask out the last handle status
    observed &= ~(__MX_OBJECT_LAST_HANDLE);

    if (status != ERR_TIMED_OUT) {
        return status;
    }
    if (expected != observed) {
        unittest_printf_critical("Observed state (%08x) != expected state (%08x)\n",
                                 observed, expected);
        return ERR_BAD_STATE;
    }
    return NO_ERROR;
}

extern mx_handle_t root_resource;

static bool test_resource_actions(void) {
    BEGIN_TEST;

    mx_handle_t rrh = root_resource;
    ASSERT_NEQ(rrh, MX_HANDLE_INVALID, "no root resource handle");

    mx_rrec_t rec[8];

    // make sure the root resource is as it should be
    size_t count;
    ASSERT_EQ(mx_object_get_info(rrh, MX_INFO_RESOURCE_RECORDS, &rec, sizeof(rec), &count, 0), NO_ERROR, "");
    ASSERT_EQ(rec[0].self.type, MX_RREC_SELF, "bad self record");
    ASSERT_EQ(rec[0].self.subtype, MX_RREC_SELF_ROOT, "bad root self record");
    ASSERT_EQ(strcmp(rec[0].self.name, "root"), 0, "bad self record name");

    // test port notifications
    mx_handle_t ph;
    mx_port_packet_t pkt;
    ASSERT_EQ(mx_port_create(MX_PORT_OPT_V2, &ph), NO_ERROR, "");
    ASSERT_EQ(mx_object_wait_async(rrh, ph, 0, MX_RESOURCE_CHILD_ADDED,
                                   MX_WAIT_ASYNC_ONCE), NO_ERROR, "");
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, 0), ERR_TIMED_OUT, "");

    // verify that our signals are in the expected state after creation
    ASSERT_EQ(check_signals(rrh, MX_RESOURCE_WRITABLE), NO_ERROR, "");

    // create a child resource
    mx_rrec_t r0 = {
        .self = {
            .type = MX_RREC_SELF,
            .subtype = MX_RREC_SELF_GENERIC,
            .name = "xyzzy",
        },
    };
    mx_handle_t rh;
    mx_status_t status = mx_resource_create(rrh, &r0, 1, &rh);
    ASSERT_EQ(status, NO_ERROR, "cannot create child resource");

    // verify that we're notified about the creation
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, 0), NO_ERROR, "");
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, 0), ERR_TIMED_OUT, "");


    // create children
    mx_handle_t ch[5];
    for (int n = 0; n < 5; n++) {
        sprintf(r0.self.name, "child%d", n);
        ASSERT_EQ(mx_resource_create(rh, &r0, 1, &ch[n]), NO_ERROR, "");
    }

    // enumerate children
    ASSERT_EQ(mx_object_get_info(rh, MX_INFO_RESOURCE_CHILDREN, rec, sizeof(rec), &count, 0), NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");

    // verify that children are as expected, and can be obtained by koid
    for (int n = 0; n < 5; n++) {
        char tmp[32];
        sprintf(tmp, "child%d", n);
        ASSERT_EQ(rec[n].self.type, MX_RREC_SELF, "");
        ASSERT_EQ(strcmp(tmp, rec[n].self.name), 0, "");

        mx_info_handle_basic_t info;
        ASSERT_EQ(mx_object_get_info(ch[n], MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL), NO_ERROR, "");
        ASSERT_EQ(info.koid, rec[n].self.koid, "");
        mx_handle_close(ch[n]);

        mx_handle_t h;
        ASSERT_EQ(mx_object_get_child(rh, info.koid, MX_RIGHT_SAME_RIGHTS, &h), NO_ERROR, "");

        mx_info_handle_basic_t info2;
        ASSERT_EQ(mx_object_get_info(h, MX_INFO_HANDLE_BASIC, &info2, sizeof(info2), NULL, NULL), NO_ERROR, "");
        ASSERT_EQ(info2.koid, rec[n].self.koid, "");
        mx_handle_close(h);
    }

    // enumerate once more
    ASSERT_EQ(mx_object_get_info(rh, MX_INFO_RESOURCE_CHILDREN, rec, sizeof(rec), &count, 0), NO_ERROR, "");
    ASSERT_EQ(count, 5u, "");

    // get a handle to the middle child
    mx_handle_t h;
    ASSERT_EQ(mx_object_get_child(rh, rec[2].self.koid, MX_RIGHT_SAME_RIGHTS, &h), NO_ERROR, "");

    // verify that our signals are in the expected state
    ASSERT_EQ(check_signals(h, MX_RESOURCE_WRITABLE), NO_ERROR, "");

    ASSERT_EQ(mx_resource_destroy(h), NO_ERROR, "");

    // verify that our signals are in the expected state
    ASSERT_EQ(check_signals(h, MX_RESOURCE_WRITABLE | MX_RESOURCE_DESTROYED), NO_ERROR, "");

    // enumerate once more, to verify that it's gone
    ASSERT_EQ(mx_object_get_info(rh, MX_INFO_RESOURCE_CHILDREN, rec, sizeof(rec), &count, 0), NO_ERROR, "");
    ASSERT_EQ(count, 4u, "");
    for (int n = 0, id = 0; n < 4; n++) {
        char tmp[32];
        sprintf(tmp, "child%d", id);
        ASSERT_EQ(strcmp(tmp,rec[n].self.name), 0, "");
        id++;
        if (n == 1) {
            id++;
        }
    }

    ASSERT_EQ(mx_handle_close(h), NO_ERROR, "");

    // check that bogus children are disallowed
    r0.self.subtype = 0xabcd;
    status = mx_resource_create(rrh, &r0, 1, &rh);
    ASSERT_EQ(status, ERR_INVALID_ARGS, "creation of bogus resource succeeded");


    ASSERT_EQ(mx_object_get_info(rrh, MX_INFO_RESOURCE_CHILDREN, rec, sizeof(rec), &count, 0), NO_ERROR, "");
    END_TEST;
}

static bool test_resource_connect(void) {
    BEGIN_TEST;

    mx_handle_t rrh = root_resource;
    ASSERT_NEQ(rrh, MX_HANDLE_INVALID, "no root resource handle");

    mx_handle_t h;
    // no handle in the queue means we must wait for one
    ASSERT_EQ(mx_resource_accept(rrh, &h), ERR_SHOULD_WAIT, "");

    // should initially be writable (connect queue empty);
    ASSERT_EQ(check_signals(rrh, MX_RESOURCE_WRITABLE), NO_ERROR, "");

    mx_handle_t c0, c1;
    ASSERT_EQ(mx_channel_create(0, &c0, &c1), NO_ERROR, "");
    // verify that non-channel handle is rejected
    ASSERT_EQ(mx_resource_connect(rrh, rrh), ERR_WRONG_TYPE, "");
    // verify that a channel handle is accepted
    ASSERT_EQ(mx_resource_connect(rrh, c0), NO_ERROR, "");
    ASSERT_EQ(check_signals(rrh, MX_RESOURCE_READABLE), NO_ERROR, "");
    // verify that the accepted handle is gone and a bad handle is rejected
    ASSERT_EQ(mx_resource_connect(rrh, c0), ERR_BAD_HANDLE, "");
    // the connect queue is only one deep, so this must fail
    ASSERT_EQ(mx_resource_connect(rrh, c1), ERR_SHOULD_WAIT, "");

    mx_handle_t c;
    // accept succeeds when there's a handle in the queue
    ASSERT_EQ(mx_resource_accept(rrh, &c), NO_ERROR, "");
    // but must fail when there isn't
    ASSERT_EQ(mx_resource_accept(rrh, &h), ERR_SHOULD_WAIT, "");
    ASSERT_EQ(check_signals(rrh, MX_RESOURCE_WRITABLE), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(resource_tests)
RUN_TEST(test_resource_actions);
RUN_TEST(test_resource_connect);
END_TEST_CASE(resource_tests)
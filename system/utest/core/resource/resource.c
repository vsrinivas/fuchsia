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

    // bind the root resource to a port
    mx_handle_t ph;
    mx_io_packet_t pkt;
    ASSERT_EQ(mx_port_create(0, &ph), NO_ERROR, "");
    ASSERT_EQ(mx_port_bind(ph, 1u, rrh, MX_RESOURCE_CHILD_ADDED), NO_ERROR, "");
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, sizeof(pkt)), ERR_TIMED_OUT, "");

    // create a child resource
    mx_rrec_t r0 = {
        .self = {
            .type = MX_RREC_SELF,
            .subtype = MX_RREC_SELF_GENERIC,
            .name = "xyzzy",
        },
    };
    mx_handle_t rh;
    mx_status_t status = mx_resource_create(rrh, &r0, 1, &rh, NULL);
    ASSERT_EQ(status, NO_ERROR, "cannot create child resource");

    // verify that we're notified about the creation
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, sizeof(pkt)), NO_ERROR, "");
    ASSERT_EQ(mx_port_wait(ph, 0, &pkt, sizeof(pkt)), ERR_TIMED_OUT, "");

    // create children
    mx_handle_t ch[5];
    for (int n = 0; n < 5; n++) {
        sprintf(r0.self.name, "child%d", n);
        ASSERT_EQ(mx_resource_create(rh, &r0, 1, &ch[n], NULL), NO_ERROR, "");
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

    // check that bogus children are disallowed
    r0.self.subtype = 0xabcd;
    status = mx_resource_create(rrh, &r0, 1, &rh, NULL);
    ASSERT_EQ(status, ERR_INVALID_ARGS, "creation of bogus resource succeeded");


    ASSERT_EQ(mx_object_get_info(rrh, MX_INFO_RESOURCE_CHILDREN, rec, sizeof(rec), &count, 0), NO_ERROR, "");
    END_TEST;
}

BEGIN_TEST_CASE(resource_tests)
RUN_TEST(test_resource_actions);
END_TEST_CASE(resource_tests)
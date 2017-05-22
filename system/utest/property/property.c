// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <magenta/compiler.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/threads.h>
#include <mxio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

static bool get_rights(mx_handle_t handle, mx_rights_t* rights)
{
    mx_info_handle_basic_t info;
    ASSERT_EQ(mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL), NO_ERROR, "");
    *rights = info.rights;
    return true;
}

static bool get_new_rights(mx_handle_t handle, mx_rights_t new_rights, mx_handle_t* new_handle)
{
    ASSERT_EQ(mx_handle_duplicate(handle, new_rights, new_handle), NO_ERROR, "");
    return true;
}

// |object| must have MX_RIGHT_{GET,SET}_PROPERTY.

static bool test_name_property(mx_handle_t object)
{
    char set_name[MX_MAX_NAME_LEN];
    char get_name[MX_MAX_NAME_LEN];

    // empty name
    strcpy(set_name, "");
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, strlen(set_name)),
              NO_ERROR, "");
    EXPECT_EQ(mx_object_get_property(object, MX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              NO_ERROR, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // largest possible name
    memset(set_name, 'x', sizeof(set_name) - 1);
    set_name[sizeof(set_name) - 1] = '\0';
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, strlen(set_name)),
              NO_ERROR, "");
    EXPECT_EQ(mx_object_get_property(object, MX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              NO_ERROR, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // too large a name by 1
    memset(set_name, 'x', sizeof(set_name));
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, sizeof(set_name)),
              NO_ERROR, "");

    mx_rights_t current_rights;
    if (get_rights(object, &current_rights))
    {
        mx_rights_t cant_set_rights = current_rights &= ~MX_RIGHT_SET_PROPERTY;
        mx_handle_t cant_set;
        if (get_new_rights(object, cant_set_rights, &cant_set))
        {
            EXPECT_EQ(mx_object_set_property(cant_set, MX_PROP_NAME, "", 0), ERR_ACCESS_DENIED, "");
            mx_handle_close(cant_set);
        }
    }

    return true;
}

static bool process_name_test(void)
{
    BEGIN_TEST;

    mx_handle_t self = mx_process_self();
    bool success = test_name_property(self);
    if (!success)
        return false;

    END_TEST;
}

static bool thread_name_test(void)
{
    BEGIN_TEST;

    mx_handle_t main_thread = thrd_get_mx_handle(thrd_current());
    unittest_printf("thread handle %d\n", main_thread);
    bool success = test_name_property(main_thread);
    if (!success)
        return false;

    END_TEST;
}

static bool vmo_name_test(void) {
    BEGIN_TEST;

    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(16, 0u, &vmo), NO_ERROR, "");
    unittest_printf("VMO handle %d\n", vmo);

    // Name should start out empty.
    char name[MX_MAX_NAME_LEN] = { 'x', '\0' };
    EXPECT_EQ(mx_object_get_property(vmo, MX_PROP_NAME, name, sizeof(name)),
              NO_ERROR, "");
    EXPECT_EQ(strcmp("", name), 0, "");

    // Check the rest.
    bool success = test_name_property(vmo);
    if (!success)
        return false;

    END_TEST;
}

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(vmo_name_test);
END_TEST_CASE(property_tests)

int main(int argc, char **argv)
{
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

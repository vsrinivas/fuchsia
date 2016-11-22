// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/threads.h>
#include <mxio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

static mx_handle_t get_main_thread(void)
{
    static mx_handle_t main_thread = MX_HANDLE_INVALID;

    // This is the only way to get the handle of the main thread,
    // and it is a one-shot deal.
    if (main_thread == MX_HANDLE_INVALID)
        main_thread = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_THREAD_SELF, 0));
    if (main_thread == MX_HANDLE_INVALID)
    {
        unittest_printf_critical("Unable to obtain main thread handle.\n");
        exit(1);
    }

    return main_thread;
}

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

    mx_handle_t main_thread = get_main_thread();
    unittest_printf("thread handle %d\n", main_thread);
    bool success = test_name_property(main_thread);
    if (!success)
        return false;

    END_TEST;
}

// This test is possible today because there exists an object that
// has properties that can be get/set but not the name property.

static bool unsupported_test(void)
{
    BEGIN_TEST;

    mx_handle_t producer;
    mx_handle_t consumer;
    producer = mx_datapipe_create(0u, 1u, 16, &consumer);
    ASSERT_GT(producer, 0, "could not create producer data pipe");
    ASSERT_GT(consumer, 0, "could not create consumer data pipe");

    char name[MX_MAX_NAME_LEN];
    const char* test_name = "test_name";
    EXPECT_EQ(mx_object_set_property(producer, MX_PROP_NAME,
                                     test_name, strlen(test_name)),
              ERR_NOT_SUPPORTED, "");

    // All objects with readable properties return a name,
    // thought it'll be an empty name if the object has no
    // actual name storage.
    EXPECT_EQ(mx_object_get_property(producer, MX_PROP_NAME,
                                     name, sizeof(name)),
              NO_ERROR, "");

    tu_handle_close(producer);
    tu_handle_close(consumer);

    END_TEST;
}

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(unsupported_test);
END_TEST_CASE(property_tests)

int main(int argc, char **argv)
{
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/dlfcn.h>
#include <magenta/processargs.h>
#include <magenta/sanitizer.h>
#include <magenta/syscalls.h>
#include <mxio/loader-service.h>
#include <unittest/unittest.h>

#define TEST_SINK_NAME "test-sink"

static atomic_bool my_loader_service_ok;
static atomic_int my_loader_service_calls;

static mx_handle_t sink_test_loader_service(void* arg, uint32_t load_op,
                                            mx_handle_t request_handle,
                                            const char* name) {
    ++my_loader_service_calls;

    EXPECT_EQ(load_op, (uint32_t)LOADER_SVC_OP_PUBLISH_DATA_SINK,
              "called with unexpected load op");

    EXPECT_STR_EQ(TEST_SINK_NAME, name, sizeof(TEST_SINK_NAME) - 1,
                  "called with unexpected name");

    EXPECT_NEQ(request_handle, MX_HANDLE_INVALID,
              "called with invalid handle");

    char vmo_name[MX_MAX_NAME_LEN];
    EXPECT_EQ(mx_object_get_property(request_handle, MX_PROP_NAME,
                                     vmo_name, sizeof(vmo_name)),
              MX_OK, "get MX_PROP_NAME");
    EXPECT_STR_EQ(TEST_SINK_NAME, vmo_name, sizeof(vmo_name),
                  "not called with expected VMO handle");

    EXPECT_EQ(mx_handle_close(request_handle), MX_OK, "");

    my_loader_service_ok = current_test_info->all_ok;
    return MX_HANDLE_INVALID;
}

bool publish_data_test(void) {
    BEGIN_TEST;
    my_loader_service_ok = false;
    my_loader_service_calls = 0;

    // Spin up our test service.
    mx_handle_t my_service =
        mxio_loader_service(&sink_test_loader_service, NULL);
    ASSERT_GT(my_service, 0, "mxio_loader_service");

    // Install the service.
    mx_handle_t old = dl_set_loader_service(my_service);
    EXPECT_GT(old, 0, "dl_set_loader_service");

    // Make up a VMO to publish.
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(0, 0, &vmo), MX_OK, "");
    EXPECT_EQ(mx_object_set_property(vmo, MX_PROP_NAME,
                                     TEST_SINK_NAME, sizeof(TEST_SINK_NAME)),
              MX_OK, "");

    // Publish the VMO to our data sink.
    __sanitizer_publish_data(TEST_SINK_NAME, vmo);

    EXPECT_EQ(my_loader_service_calls, 1,
              "loader-service not called exactly once");

    EXPECT_TRUE(my_loader_service_ok, "loader service thread not happy");

    // Put things back to how they were.
    mx_handle_t old2 = dl_set_loader_service(old);
    EXPECT_EQ(old2, my_service, "unexpected previous service handle");
    mx_handle_close(old2);

    END_TEST;
}

BEGIN_TEST_CASE(sanitizer_utils_tests)
RUN_TEST(publish_data_test);
END_TEST_CASE(sanitizer_utils_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

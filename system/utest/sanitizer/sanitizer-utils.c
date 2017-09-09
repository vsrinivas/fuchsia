// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/loader-service.h>
#include <magenta/dlfcn.h>
#include <magenta/processargs.h>
#include <magenta/sanitizer.h>
#include <magenta/syscalls.h>

#include <unittest/unittest.h>

#define TEST_SINK_NAME "test-sink"
#define TEST_CONFIG_GOOD_NAME "test-config-exists"
#define TEST_CONFIG_BAD_NAME "test-config-does-not-exist"

static atomic_bool my_loader_service_ok;
static atomic_int my_loader_service_calls;

static mx_status_t sink_test_loader_service(void* arg, uint32_t load_op,
                                            mx_handle_t request_handle,
                                            const char* name, mx_handle_t* out) {
    ++my_loader_service_calls;

    EXPECT_EQ(load_op, (uint32_t)LOADER_SVC_OP_PUBLISH_DATA_SINK,
              "called with unexpected load op");

    EXPECT_STR_EQ(TEST_SINK_NAME, name, sizeof(TEST_SINK_NAME) - 1,
                  "called with unexpected name");

    EXPECT_NE(request_handle, MX_HANDLE_INVALID,
             "called with invalid handle");

    char vmo_name[MX_MAX_NAME_LEN];
    EXPECT_EQ(mx_object_get_property(request_handle, MX_PROP_NAME,
                                     vmo_name, sizeof(vmo_name)),
              MX_OK, "get MX_PROP_NAME");
    EXPECT_STR_EQ(TEST_SINK_NAME, vmo_name, sizeof(vmo_name),
                  "not called with expected VMO handle");

    EXPECT_EQ(mx_handle_close(request_handle), MX_OK, "");

    my_loader_service_ok = current_test_info->all_ok;
    return MX_OK;
}

bool publish_data_test(void) {
    BEGIN_TEST;
    my_loader_service_ok = false;
    my_loader_service_calls = 0;

    // Spin up our test service.
    mx_handle_t my_service;
    mx_status_t status = loader_service_simple(&sink_test_loader_service, NULL, &my_service);
    ASSERT_EQ(status, MX_OK, "mxio_loader_service");

    // Install the service.
    mx_handle_t old = dl_set_loader_service(my_service);
    EXPECT_NE(old, MX_HANDLE_INVALID, "dl_set_loader_service");

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

static mx_handle_t test_config_vmo = MX_HANDLE_INVALID;

static mx_status_t config_test_loader_service(void* arg, uint32_t load_op,
                                              mx_handle_t request_handle,
                                              const char* name,
                                              mx_handle_t* out) {
    ++my_loader_service_calls;

    EXPECT_EQ(load_op, (uint32_t)LOADER_SVC_OP_LOAD_DEBUG_CONFIG,
              "called with unexpected load op");

    EXPECT_EQ(request_handle, MX_HANDLE_INVALID, "called with a handle");

    mx_handle_t result = MX_HANDLE_INVALID;
    if (!strcmp(TEST_CONFIG_GOOD_NAME, name)) {
        EXPECT_NE(test_config_vmo, MX_HANDLE_INVALID, "");
        *out = test_config_vmo;
        result = MX_OK;
    } else {
        EXPECT_STR_EQ(TEST_CONFIG_BAD_NAME,
                      name, sizeof(TEST_CONFIG_BAD_NAME) - 1,
                      "called with unexpected name");
        result = MX_ERR_NOT_FOUND;
    }

    my_loader_service_ok = current_test_info->all_ok;
    return result;
}

bool debug_config_test(void) {
    BEGIN_TEST;
    my_loader_service_ok = false;
    my_loader_service_calls = 0;

    // Spin up our test service.
    mx_handle_t my_service;
    mx_status_t status =
        loader_service_simple(&config_test_loader_service, NULL, &my_service);
    ASSERT_EQ(status, MX_OK, "mxio_loader_service");

    // Install the service.
    mx_handle_t old = dl_set_loader_service(my_service);
    EXPECT_NE(old, MX_HANDLE_INVALID, "dl_set_loader_service");

    // Make up a VMO that we'll get back from the service.
    ASSERT_EQ(mx_vmo_create(0, 0, &test_config_vmo), MX_OK, "");

    // Test the success case.
    mx_handle_t vmo = MX_HANDLE_INVALID;
    EXPECT_EQ(__sanitizer_get_configuration(TEST_CONFIG_GOOD_NAME, &vmo),
              MX_OK, "__sanitizer_get_configuration on valid name");
    EXPECT_EQ(vmo, test_config_vmo, "not the expected VMO handle");

    EXPECT_EQ(my_loader_service_calls, 1,
              "loader-service not called exactly once");
    EXPECT_TRUE(my_loader_service_ok, "loader service thread not happy");

    EXPECT_EQ(mx_handle_close(test_config_vmo), MX_OK, "");
    test_config_vmo = MX_HANDLE_INVALID;

    my_loader_service_ok = false;
    my_loader_service_calls = 0;

    // Test the failure case.
    EXPECT_EQ(__sanitizer_get_configuration(TEST_CONFIG_BAD_NAME, &vmo),
              MX_ERR_NOT_FOUND,
              "__sanitizer_get_configuration on invalid name");

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
RUN_TEST(debug_config_test);
END_TEST_CASE(sanitizer_utils_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

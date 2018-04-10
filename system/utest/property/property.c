// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>
#include <fdio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

static bool get_rights(zx_handle_t handle, zx_rights_t* rights) {
    zx_info_handle_basic_t info;
    ASSERT_EQ(zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    *rights = info.rights;
    return true;
}

static bool get_new_rights(zx_handle_t handle, zx_rights_t new_rights, zx_handle_t* new_handle) {
    ASSERT_EQ(zx_handle_duplicate(handle, new_rights, new_handle), ZX_OK, "");
    return true;
}

// |object| must have ZX_RIGHT_{GET,SET}_PROPERTY.

static bool test_name_property(zx_handle_t object) {
    char set_name[ZX_MAX_NAME_LEN];
    char get_name[ZX_MAX_NAME_LEN];

    // empty name
    strcpy(set_name, "");
    EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME,
                                     set_name, strlen(set_name)),
              ZX_OK, "");
    EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              ZX_OK, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // largest possible name
    memset(set_name, 'x', sizeof(set_name) - 1);
    set_name[sizeof(set_name) - 1] = '\0';
    EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME,
                                     set_name, strlen(set_name)),
              ZX_OK, "");
    EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              ZX_OK, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // too large a name by 1
    memset(set_name, 'x', sizeof(set_name));
    EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME,
                                     set_name, sizeof(set_name)),
              ZX_OK, "");

    zx_rights_t current_rights;
    if (get_rights(object, &current_rights)) {
        zx_rights_t cant_set_rights = current_rights &= ~ZX_RIGHT_SET_PROPERTY;
        zx_handle_t cant_set;
        if (get_new_rights(object, cant_set_rights, &cant_set)) {
            EXPECT_EQ(zx_object_set_property(cant_set, ZX_PROP_NAME, "", 0), ZX_ERR_ACCESS_DENIED, "");
            zx_handle_close(cant_set);
        }
    }

    return true;
}

static bool process_name_test(void) {
    BEGIN_TEST;

    zx_handle_t self = zx_process_self();
    bool success = test_name_property(self);
    if (!success)
        return false;

    END_TEST;
}

static bool thread_name_test(void) {
    BEGIN_TEST;

    zx_handle_t main_thread = thrd_get_zx_handle(thrd_current());
    unittest_printf("thread handle %d\n", main_thread);
    bool success = test_name_property(main_thread);
    if (!success)
        return false;

    END_TEST;
}

static bool vmo_name_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(16, 0u, &vmo), ZX_OK, "");
    unittest_printf("VMO handle %d\n", vmo);

    // Name should start out empty.
    char name[ZX_MAX_NAME_LEN] = {'x', '\0'};
    EXPECT_EQ(zx_object_get_property(vmo, ZX_PROP_NAME, name, sizeof(name)),
              ZX_OK, "");
    EXPECT_EQ(strcmp("", name), 0, "");

    // Check the rest.
    bool success = test_name_property(vmo);
    if (!success)
        return false;

    END_TEST;
}

// Returns a job, its child job, and its grandchild job.
#define NUM_TEST_JOBS 3
static zx_status_t get_test_jobs(zx_handle_t jobs_out[NUM_TEST_JOBS]) {
    static zx_handle_t test_jobs[NUM_TEST_JOBS] = {ZX_HANDLE_INVALID};

    if (test_jobs[0] == ZX_HANDLE_INVALID) {
        zx_handle_t root;
        zx_status_t s = zx_job_create(zx_job_default(), 0, &root);
        if (s != ZX_OK) {
            EXPECT_EQ(s, ZX_OK, "root job"); // Poison the test
            return s;
        }
        zx_handle_t child;
        s = zx_job_create(root, 0, &child);
        if (s != ZX_OK) {
            EXPECT_EQ(s, ZX_OK, "child job");
            zx_task_kill(root);
            zx_handle_close(root);
            return s;
        }
        zx_handle_t gchild;
        s = zx_job_create(child, 0, &gchild);
        if (s != ZX_OK) {
            EXPECT_EQ(s, ZX_OK, "grandchild job");
            zx_task_kill(root); // Kills child, too
            zx_handle_close(child);
            zx_handle_close(root);
            return s;
        }
        test_jobs[0] = root;
        test_jobs[1] = child;
        test_jobs[2] = gchild;
    }
    memcpy(jobs_out, test_jobs, sizeof(test_jobs));
    return ZX_OK;
}

static bool assert_test_jobs_importance(
    zx_job_importance_t root_imp, zx_job_importance_t child_imp,
    zx_job_importance_t gchild_imp) {

    zx_handle_t jobs[NUM_TEST_JOBS];
    ASSERT_EQ(get_test_jobs(jobs), ZX_OK, "");

    zx_job_importance_t importance = 0xffff;
    EXPECT_EQ(zx_object_get_property(jobs[0], ZX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_EQ(root_imp, importance, "");

    importance = 0xffff;
    EXPECT_EQ(zx_object_get_property(jobs[1], ZX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_EQ(child_imp, importance, "");

    importance = 0xffff;
    EXPECT_EQ(zx_object_get_property(jobs[2], ZX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_EQ(gchild_imp, importance, "");

    return true;
}

static bool importance_smoke_test(void) {
    BEGIN_TEST;

    zx_handle_t jobs[NUM_TEST_JOBS];
    ASSERT_EQ(get_test_jobs(jobs), ZX_OK, "");

    static const zx_job_importance_t kImpInherited =
        ZX_JOB_IMPORTANCE_INHERITED;
    static const zx_job_importance_t kImpMin = ZX_JOB_IMPORTANCE_MIN;
    static const zx_job_importance_t kImpHalf =
        (ZX_JOB_IMPORTANCE_MAX - ZX_JOB_IMPORTANCE_MIN) / 2 +
        ZX_JOB_IMPORTANCE_MIN;
    static const zx_job_importance_t kImpMax = ZX_JOB_IMPORTANCE_MAX;

    // Set all to the same importance.
    for (int i = 0; i < NUM_TEST_JOBS; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%d] handle %d", i, jobs[i]);
        EXPECT_EQ(zx_object_set_property(jobs[i], ZX_PROP_JOB_IMPORTANCE,
                                         &kImpMax,
                                         sizeof(zx_job_importance_t)),
                  ZX_OK, msg);
    }
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpMax, kImpMax),
                "");

    // Tweak the child.
    EXPECT_EQ(zx_object_set_property(jobs[1], ZX_PROP_JOB_IMPORTANCE,
                                     &kImpHalf, sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpMax),
                "");

    // Tweak the grandchild.
    EXPECT_EQ(zx_object_set_property(jobs[2], ZX_PROP_JOB_IMPORTANCE,
                                     &kImpMin, sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpMin),
                "");

    // Setting the grandchild to "inherited" should make it look like it
    // has the child's importance.
    EXPECT_EQ(zx_object_set_property(jobs[2], ZX_PROP_JOB_IMPORTANCE,
                                     &kImpInherited,
                                     sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpHalf),
                "");

    // Setting the child to "inherited" should cause both child and grandchild
    // to pick up the root importance.
    EXPECT_EQ(zx_object_set_property(jobs[1], ZX_PROP_JOB_IMPORTANCE,
                                     &kImpInherited,
                                     sizeof(zx_job_importance_t)),
              ZX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpMax, kImpMax),
                "");

    END_TEST;
}

static bool bad_importance_value_fails(void) {
    BEGIN_TEST;

    zx_handle_t job;
    {
        zx_handle_t jobs[NUM_TEST_JOBS];
        ASSERT_EQ(get_test_jobs(jobs), ZX_OK, "");
        // Only need one job.
        job = jobs[0];
    }

    zx_job_importance_t bad_values[] = {
        -3,
        -2,
        256,
        4096,
    };

    for (size_t i = 0; i < countof(bad_values); i++) {
        zx_job_importance_t bad_value = bad_values[i];
        char msg[32];
        snprintf(msg, sizeof(msg), "bad value %" PRId32, bad_value);
        EXPECT_EQ(zx_object_set_property(job, ZX_PROP_JOB_IMPORTANCE,
                                         &bad_value,
                                         sizeof(zx_job_importance_t)),
                  ZX_ERR_OUT_OF_RANGE, msg);
    }

    END_TEST;
}

static bool socket_buffer_test(void) {
    BEGIN_TEST;

    zx_handle_t sockets[2];
    ASSERT_EQ(zx_socket_create(0, &sockets[0], &sockets[1]), ZX_OK, "");

    // Check the default state of the properties.
    size_t value;
    struct {
        uint32_t max_prop;
        uint32_t size_prop;
    } props[] = {
        {ZX_PROP_SOCKET_RX_BUF_MAX, ZX_PROP_SOCKET_RX_BUF_SIZE},
        {ZX_PROP_SOCKET_TX_BUF_MAX, ZX_PROP_SOCKET_TX_BUF_SIZE},
    };
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            ASSERT_EQ(zx_object_get_property(sockets[i], props[j].max_prop, &value, sizeof(value)),
                      ZX_OK, "");
            EXPECT_GT(value, 0u, "");
            ASSERT_EQ(zx_object_get_property(sockets[i], props[j].size_prop, &value, sizeof(value)),
                      ZX_OK, "");
            EXPECT_EQ(value, 0u, "");
        }
    }

    // Check the buffer size after a write.
    uint8_t buf[8] = {};
    size_t actual;
    ASSERT_EQ(zx_socket_write(sockets[1], 0, buf, sizeof(buf), &actual), ZX_OK, "");
    EXPECT_EQ(actual, sizeof(buf), "");

    ASSERT_EQ(zx_object_get_property(sockets[0], ZX_PROP_SOCKET_RX_BUF_SIZE, &value, sizeof(value)),
              ZX_OK, "");
    EXPECT_EQ(value, sizeof(buf), "");
    ASSERT_EQ(zx_object_get_property(sockets[1], ZX_PROP_SOCKET_TX_BUF_SIZE, &value, sizeof(value)),
              ZX_OK, "");
    EXPECT_EQ(value, sizeof(buf), "");

    END_TEST;
}

static bool channel_depth_test(void) {
    BEGIN_TEST;

    zx_handle_t channels[2];
    ASSERT_EQ(zx_channel_create(0, &channels[0], &channels[1]), ZX_OK, "");

    for (int idx = 0; idx < 2; ++idx) {
        size_t depth = 0u;
        zx_status_t status = zx_object_get_property(channels[idx], ZX_PROP_CHANNEL_TX_MSG_MAX,
                                                    &depth, sizeof(depth));
        ASSERT_EQ(status, ZX_OK, "");
        // For now, just check that the depth is non-zero.
        ASSERT_NE(depth, 0u, "");
    }

    END_TEST;
}

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(vmo_name_test);
RUN_TEST(importance_smoke_test);
RUN_TEST(bad_importance_value_fails);
RUN_TEST(socket_buffer_test);
RUN_TEST(channel_depth_test);
END_TEST_CASE(property_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
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

static bool get_rights(mx_handle_t handle, mx_rights_t* rights) {
    mx_info_handle_basic_t info;
    ASSERT_EQ(mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL), MX_OK, "");
    *rights = info.rights;
    return true;
}

static bool get_new_rights(mx_handle_t handle, mx_rights_t new_rights, mx_handle_t* new_handle) {
    ASSERT_EQ(mx_handle_duplicate(handle, new_rights, new_handle), MX_OK, "");
    return true;
}

// |object| must have MX_RIGHT_{GET,SET}_PROPERTY.

static bool test_name_property(mx_handle_t object) {
    char set_name[MX_MAX_NAME_LEN];
    char get_name[MX_MAX_NAME_LEN];

    // empty name
    strcpy(set_name, "");
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, strlen(set_name)),
              MX_OK, "");
    EXPECT_EQ(mx_object_get_property(object, MX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              MX_OK, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // largest possible name
    memset(set_name, 'x', sizeof(set_name) - 1);
    set_name[sizeof(set_name) - 1] = '\0';
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, strlen(set_name)),
              MX_OK, "");
    EXPECT_EQ(mx_object_get_property(object, MX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              MX_OK, "");
    EXPECT_EQ(strcmp(get_name, set_name), 0, "");

    // too large a name by 1
    memset(set_name, 'x', sizeof(set_name));
    EXPECT_EQ(mx_object_set_property(object, MX_PROP_NAME,
                                     set_name, sizeof(set_name)),
              MX_OK, "");

    mx_rights_t current_rights;
    if (get_rights(object, &current_rights)) {
        mx_rights_t cant_set_rights = current_rights &= ~MX_RIGHT_SET_PROPERTY;
        mx_handle_t cant_set;
        if (get_new_rights(object, cant_set_rights, &cant_set)) {
            EXPECT_EQ(mx_object_set_property(cant_set, MX_PROP_NAME, "", 0), MX_ERR_ACCESS_DENIED, "");
            mx_handle_close(cant_set);
        }
    }

    return true;
}

static bool process_name_test(void) {
    BEGIN_TEST;

    mx_handle_t self = mx_process_self();
    bool success = test_name_property(self);
    if (!success)
        return false;

    END_TEST;
}

static bool thread_name_test(void) {
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
    ASSERT_EQ(mx_vmo_create(16, 0u, &vmo), MX_OK, "");
    unittest_printf("VMO handle %d\n", vmo);

    // Name should start out empty.
    char name[MX_MAX_NAME_LEN] = {'x', '\0'};
    EXPECT_EQ(mx_object_get_property(vmo, MX_PROP_NAME, name, sizeof(name)),
              MX_OK, "");
    EXPECT_EQ(strcmp("", name), 0, "");

    // Check the rest.
    bool success = test_name_property(vmo);
    if (!success)
        return false;

    END_TEST;
}

// Returns a job, its child job, and its grandchild job.
#define NUM_TEST_JOBS 3
static mx_status_t get_test_jobs(mx_handle_t jobs_out[NUM_TEST_JOBS]) {
    static mx_handle_t test_jobs[NUM_TEST_JOBS] = {MX_HANDLE_INVALID};

    if (test_jobs[0] == MX_HANDLE_INVALID) {
        mx_handle_t root;
        mx_status_t s = mx_job_create(mx_job_default(), 0, &root);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "root job"); // Poison the test
            return s;
        }
        mx_handle_t child;
        s = mx_job_create(root, 0, &child);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "child job");
            mx_task_kill(root);
            mx_handle_close(root);
            return s;
        }
        mx_handle_t gchild;
        s = mx_job_create(child, 0, &gchild);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "grandchild job");
            mx_task_kill(root); // Kills child, too
            mx_handle_close(child);
            mx_handle_close(root);
            return s;
        }
        test_jobs[0] = root;
        test_jobs[1] = child;
        test_jobs[2] = gchild;
    }
    memcpy(jobs_out, test_jobs, sizeof(test_jobs));
    return MX_OK;
}

static bool assert_test_jobs_importance(
    mx_job_importance_t root_imp, mx_job_importance_t child_imp,
    mx_job_importance_t gchild_imp) {

    mx_handle_t jobs[NUM_TEST_JOBS];
    ASSERT_EQ(get_test_jobs(jobs), MX_OK, "");

    mx_job_importance_t importance = 0xffff;
    EXPECT_EQ(mx_object_get_property(jobs[0], MX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_EQ(root_imp, importance, "");

    importance = 0xffff;
    EXPECT_EQ(mx_object_get_property(jobs[1], MX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_EQ(child_imp, importance, "");

    importance = 0xffff;
    EXPECT_EQ(mx_object_get_property(jobs[2], MX_PROP_JOB_IMPORTANCE,
                                     &importance, sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_EQ(gchild_imp, importance, "");

    return true;
}

static bool importance_smoke_test(void) {
    BEGIN_TEST;

    mx_handle_t jobs[NUM_TEST_JOBS];
    ASSERT_EQ(get_test_jobs(jobs), MX_OK, "");

    static const mx_job_importance_t kImpInherited =
        MX_JOB_IMPORTANCE_INHERITED;
    static const mx_job_importance_t kImpMin = MX_JOB_IMPORTANCE_MIN;
    static const mx_job_importance_t kImpHalf =
        (MX_JOB_IMPORTANCE_MAX - MX_JOB_IMPORTANCE_MIN) / 2 +
        MX_JOB_IMPORTANCE_MIN;
    static const mx_job_importance_t kImpMax = MX_JOB_IMPORTANCE_MAX;

    // Set all to the same importance.
    for (int i = 0; i < NUM_TEST_JOBS; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%d] handle %d", i, jobs[i]);
        EXPECT_EQ(mx_object_set_property(jobs[i], MX_PROP_JOB_IMPORTANCE,
                                         &kImpMax,
                                         sizeof(mx_job_importance_t)),
                  MX_OK, msg);
    }
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpMax, kImpMax),
                "");

    // Tweak the child.
    EXPECT_EQ(mx_object_set_property(jobs[1], MX_PROP_JOB_IMPORTANCE,
                                     &kImpHalf, sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpMax),
                "");

    // Tweak the grandchild.
    EXPECT_EQ(mx_object_set_property(jobs[2], MX_PROP_JOB_IMPORTANCE,
                                     &kImpMin, sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpMin),
                "");

    // Setting the grandchild to "inherited" should make it look like it
    // has the child's importance.
    EXPECT_EQ(mx_object_set_property(jobs[2], MX_PROP_JOB_IMPORTANCE,
                                     &kImpInherited,
                                     sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpHalf, kImpHalf),
                "");

    // Setting the child to "inherited" should cause both child and grandchild
    // to pick up the root importance.
    EXPECT_EQ(mx_object_set_property(jobs[1], MX_PROP_JOB_IMPORTANCE,
                                     &kImpInherited,
                                     sizeof(mx_job_importance_t)),
              MX_OK, "");
    EXPECT_TRUE(assert_test_jobs_importance(
                    kImpMax, kImpMax, kImpMax),
                "");

    END_TEST;
}

static bool bad_importance_value_fails(void) {
    BEGIN_TEST;

    mx_handle_t job;
    {
        mx_handle_t jobs[NUM_TEST_JOBS];
        ASSERT_EQ(get_test_jobs(jobs), MX_OK, "");
        // Only need one job.
        job = jobs[0];
    }

    mx_job_importance_t bad_values[] = {
        -3,
        -2,
        256,
        4096,
    };

    for (size_t i = 0; i < countof(bad_values); i++) {
        mx_job_importance_t bad_value = bad_values[i];
        char msg[32];
        snprintf(msg, sizeof(msg), "bad value %" PRId32, bad_value);
        EXPECT_EQ(mx_object_set_property(job, MX_PROP_JOB_IMPORTANCE,
                                         &bad_value,
                                         sizeof(mx_job_importance_t)),
                  MX_ERR_OUT_OF_RANGE, msg);
    }

    END_TEST;
}

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(vmo_name_test);
RUN_TEST(importance_smoke_test);
RUN_TEST(bad_importance_value_fails);
END_TEST_CASE(property_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <lib/fdio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>

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

    // name with extra garbage at the end
    memset(set_name, 'A', sizeof(set_name));
    set_name[1] = '\0';

    EXPECT_EQ(zx_object_set_property(object, ZX_PROP_NAME,
                                     set_name, sizeof(set_name)),
              ZX_OK, "");
    EXPECT_EQ(zx_object_get_property(object, ZX_PROP_NAME,
                                     get_name, sizeof(get_name)),
              ZX_OK, "");
    EXPECT_EQ(get_name[0], 'A', "");
    for (size_t i = 1; i < sizeof(get_name); i++) {
        EXPECT_EQ(get_name[i], '\0', "");
    }

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

static bool job_name_test(void) {
    BEGIN_TEST;

    zx_handle_t testjob;
    zx_status_t s = zx_job_create(zx_job_default(), 0, &testjob);
    EXPECT_EQ(s, ZX_OK, "");

    bool success = test_name_property(testjob);
    if (!success)
        return false;

    zx_handle_close(testjob);
    END_TEST;
}

static bool channel_name_test(void) {
    BEGIN_TEST;

    zx_handle_t channel1;
    zx_handle_t channel2;
    zx_status_t s = zx_channel_create(0, &channel1, &channel2);
    EXPECT_EQ(s, ZX_OK, "");

    char name[ZX_MAX_NAME_LEN];

    memset(name, 'A', sizeof(name));
    EXPECT_EQ(zx_object_get_property(channel1, ZX_PROP_NAME,
                                     name, sizeof(name)),
              ZX_OK, "");
    for (size_t i = 0; i < sizeof(name); i++) {
        EXPECT_EQ(name[i], '\0', "");
    }

    memset(name, 'A', sizeof(name));
    EXPECT_EQ(zx_object_get_property(channel2, ZX_PROP_NAME,
                                     name, sizeof(name)),
              ZX_OK, "");
    for (size_t i = 0; i < sizeof(name); i++) {
        EXPECT_EQ(name[i], '\0', "");
    }

    zx_handle_close(channel1);
    zx_handle_close(channel2);
    END_TEST;
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

    char name[ZX_MAX_NAME_LEN];
    memset(name, 'A', sizeof(name));

    // Name should start out empty.
    EXPECT_EQ(zx_object_get_property(vmo, ZX_PROP_NAME, name, sizeof(name)),
              ZX_OK, "");
    for (size_t i = 0; i < sizeof(name); i++) {
        EXPECT_EQ(name[i], '\0', "");
    }

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

    // Check TX buf goes to zero on peer closed.
    zx_handle_close(sockets[0]);
    ASSERT_EQ(zx_object_get_property(sockets[1], ZX_PROP_SOCKET_TX_BUF_SIZE, &value, sizeof(value)),
              ZX_OK, "");
    EXPECT_EQ(value, 0u, "");
    ASSERT_EQ(zx_object_get_property(sockets[1], ZX_PROP_SOCKET_TX_BUF_MAX, &value, sizeof(value)),
              ZX_OK, "");
    EXPECT_EQ(value, 0u, "");

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

#if defined(__x86_64__)

static uintptr_t read_gs(void) {
    uintptr_t gs;
    __asm__ __volatile__("mov %%gs:0,%0"
                         : "=r"(gs));
    return gs;
}

static int do_nothing(void* unused) {
    for (;;) {
    }
    return 0;
}

static bool fs_invalid_test(void) {
    BEGIN_TEST;

    // The success case of fs is hard to explicitly test, but is
    // exercised all the time (ie userspace would explode instantly if
    // it was broken). Since we will be soon adding a corresponding
    // mechanism for gs, don't worry about testing success.

    uintptr_t fs_storage;
    uintptr_t fs_location = (uintptr_t)&fs_storage;

    // All the failures:

    // Try a thread other than the current one.
    thrd_t t;
    int success = thrd_create(&t, &do_nothing, NULL);
    ASSERT_EQ(success, thrd_success, "");
    zx_handle_t other_thread = thrd_get_zx_handle(t);
    zx_status_t status = zx_object_set_property(other_thread, ZX_PROP_REGISTER_FS,
                                                &fs_location, sizeof(fs_location));
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

    // Try a non-thread object type.
    status = zx_object_set_property(zx_process_self(), ZX_PROP_REGISTER_FS,
                                    &fs_location, sizeof(fs_location));
    ASSERT_EQ(status, ZX_ERR_WRONG_TYPE, "");

    // Not enough buffer to hold the property value.
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS,
                                    &fs_location, sizeof(fs_location) - 1);
    ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "");

    // A non-canonical vaddr.
    uintptr_t noncanonical_fs_location = fs_location | (1ull << 47);
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS,
                                    &noncanonical_fs_location,
                                    sizeof(noncanonical_fs_location));
    ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

    // A non-userspace vaddr.
    uintptr_t nonuserspace_fs_location = 0xffffffff40000000;
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_FS,
                                    &nonuserspace_fs_location,
                                    sizeof(nonuserspace_fs_location));
    ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

static bool gs_test(void) {
    BEGIN_TEST;

    // First test the success case.
    const uintptr_t expected = 0xfeedfacefeedface;

    uintptr_t gs_storage = expected;
    uintptr_t gs_location = (uintptr_t)&gs_storage;

    zx_status_t status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS,
                                                &gs_location, sizeof(gs_location));
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(read_gs(), expected, "");

    // All the failures:

    // Try a thread other than the current one.
    thrd_t t;
    int success = thrd_create(&t, &do_nothing, NULL);
    ASSERT_EQ(success, thrd_success, "");
    zx_handle_t other_thread = thrd_get_zx_handle(t);
    status = zx_object_set_property(other_thread, ZX_PROP_REGISTER_GS,
                                    &gs_location, sizeof(gs_location));
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

    // Try a non-thread object type.
    status = zx_object_set_property(zx_process_self(), ZX_PROP_REGISTER_GS,
                                    &gs_location, sizeof(gs_location));
    ASSERT_EQ(status, ZX_ERR_WRONG_TYPE, "");

    // Not enough buffer to hold the property value.
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS,
                                    &gs_location, sizeof(gs_location) - 1);
    ASSERT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL, "");

    // A non-canonical vaddr.
    uintptr_t noncanonical_gs_location = gs_location | (1ull << 47);
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS,
                                    &noncanonical_gs_location,
                                    sizeof(noncanonical_gs_location));
    ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

    // A non-userspace vaddr.
    uintptr_t nonuserspace_gs_location = 0xffffffff40000000;
    status = zx_object_set_property(zx_thread_self(), ZX_PROP_REGISTER_GS,
                                    &nonuserspace_gs_location,
                                    sizeof(nonuserspace_gs_location));
    ASSERT_EQ(status, ZX_ERR_INVALID_ARGS, "");

    END_TEST;
}

#endif // defined(__x86_64__)

BEGIN_TEST_CASE(property_tests)
RUN_TEST(process_name_test);
RUN_TEST(thread_name_test);
RUN_TEST(job_name_test);
RUN_TEST(vmo_name_test);
RUN_TEST(channel_name_test);
RUN_TEST(socket_buffer_test);
RUN_TEST(channel_depth_test);
#if defined(__x86_64__)
RUN_TEST(fs_invalid_test)
RUN_TEST(gs_test)
#endif
END_TEST_CASE(property_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}

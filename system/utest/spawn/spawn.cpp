// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fcntl.h>
#include <fdio/spawn.h>
#include <fdio/util.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/limits.h>
#include <zircon/processargs.h>

static constexpr char kSpawnChild[] = "/boot/bin/spawn-child";

static int64_t join(const zx::process& process) {
    zx_status_t status = process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
    ASSERT_EQ(ZX_OK, status);
    zx_info_process_t proc_info{};
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    ASSERT_EQ(ZX_OK, status);
    return proc_info.return_code;
}

static bool spawn_control_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        const char* argv[] = {kSpawnChild, nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(43, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--argc", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(2, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--argc", "three", "four", "five", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(5, join(process));
    }

    END_TEST;
}

static bool spawn_invalid_args_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    const char* argv[] = {kSpawnChild, nullptr};

    status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                        nullptr, argv, process.reset_and_get_address());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

    status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                        "/bogus/not/a/file", argv, process.reset_and_get_address());
    ASSERT_EQ(ZX_ERR_IO, status);

    status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                        kSpawnChild, NULL, process.reset_and_get_address());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

    status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                        kSpawnChild, argv + 1, process.reset_and_get_address());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

    END_TEST;
}

static bool spawn_flags_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        // We can't actually launch a process without FDIO_SPAWN_CLONE_LDSVC
        // because we can't load the PT_INTERP.
        const char* argv[] = {kSpawnChild, "--flags", "none", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, 0, kSpawnChild, argv,
                            process.reset_and_get_address());
        ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
        EXPECT_FALSE(process.is_valid());
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "none", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(51, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "job", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_CLONE_LDSVC,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(52, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "namespace", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC | FDIO_SPAWN_CLONE_NAMESPACE,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(53, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "stdio", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC | FDIO_SPAWN_CLONE_STDIO,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(54, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "environ", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC | FDIO_SPAWN_CLONE_ENVIRON,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(55, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--flags", "all", nullptr};
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(56, join(process));
    }

    END_TEST;
}

static bool spawn_environ_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    setenv("SPAWN_TEST_PARENT", "1", 1);

    {
        const char* argv[] = {kSpawnChild, "--env", "empty", nullptr};
        const char* env[] = {nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC,
                                kSpawnChild, argv, env, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(61, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--env", "one", nullptr};
        const char* env[] = {"SPAWN_TEST_CHILD=1", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_LDSVC,
                                kSpawnChild, argv, env, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(62, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--env", "one", nullptr};
        const char* env[] = {"SPAWN_TEST_CHILD=1", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, env, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(62, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--env", "two", nullptr};
        const char* env[] = {"SPAWN_TEST_CHILD=1", "SPAWN_TEST_CHILD2=1", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, env, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(63, join(process));
    }

    {
        const char* argv[] = {kSpawnChild, "--env", "clone", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(64, join(process));
    }

    unsetenv("SPAWN_TEST_PARENT");

    END_TEST;
}

static bool spawn_actions_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        const char* argv[] = {nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
    }

    {
        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_SET_NAME;
        action.name.data = "spawn-child-name";

        const char* argv[] = {nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(42, join(process));

        char name[ZX_MAX_NAME_LEN];
        ASSERT_EQ(ZX_OK, process.get_property(ZX_PROP_NAME, name, sizeof(name)));
        EXPECT_TRUE(!strcmp("spawn-child-name", name));
    }

    {
        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_CLONE_FD;
        action.fd.local_fd = 1;
        action.fd.target_fd = 21;

        const char* argv[] = {kSpawnChild, "--action", "clone-fd", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(71, join(process));
    }

    END_TEST;
}

BEGIN_TEST_CASE(spawn_tests)
RUN_TEST(spawn_control_test)
RUN_TEST(spawn_invalid_args_test)
RUN_TEST(spawn_flags_test)
RUN_TEST(spawn_environ_test)
RUN_TEST(spawn_actions_test)
END_TEST_CASE(spawn_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}

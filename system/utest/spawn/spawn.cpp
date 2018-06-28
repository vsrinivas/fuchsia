// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/limits.h>
#include <zircon/processargs.h>
#include <zircon/syscalls/policy.h>

static constexpr char kSpawnChild[] = "/boot/bin/spawn-child";
static constexpr char kSpawnLauncher[] = "/boot/bin/spawn-launcher";

static bool has_fd(int fd) {
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t ids[FDIO_MAX_HANDLES];
    zx_status_t status = fdio_clone_fd(fd, fd + 50, handles, ids);
    if (status > 0) {
        size_t n = static_cast<size_t>(status);
        zx_handle_close_many(handles, n);
        return true;
    }
    return false;
}

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

static bool spawn_launcher_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;
    const char* argv[] = {kSpawnLauncher, kSpawnChild, nullptr};

    // Check that we can spawn the lancher process in a job and that the
    // launcher process can launch the child.
    {
        zx::job job;
        ASSERT_EQ(ZX_OK, zx::job::create(*zx::job::default_job(), 0, &job));

        status = fdio_spawn(job.get(), FDIO_SPAWN_CLONE_ALL, kSpawnLauncher,
                            argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(43, join(process));
        ASSERT_EQ(ZX_OK, job.kill());
    }

    // Check that setting |ZX_POL_NEW_PROCESS| to |ZX_POL_ACTION_DENY| prevents
    // the launcher from launching the child.
    {
        zx::job job;
        ASSERT_EQ(ZX_OK, zx::job::create(*zx::job::default_job(), 0, &job));
        zx_policy_basic_t policy = {
            .condition = ZX_POL_NEW_PROCESS,
            .policy = ZX_POL_ACTION_DENY,
        };
        ASSERT_EQ(ZX_OK, job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, &policy, 1));

        status = fdio_spawn(job.get(), FDIO_SPAWN_CLONE_ALL, kSpawnLauncher,
                            argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(401, join(process));
        ASSERT_EQ(ZX_OK, job.kill());
    }

    END_TEST;
}

static bool spawn_invalid_args_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    const char* argv[] = {kSpawnChild, nullptr};

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

static bool spawn_actions_fd_test(void) {
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
        zx_handle_t socket = ZX_HANDLE_INVALID;
        uint32_t type;
        int fd = fdio_pipe_half(&socket, &type);
        ASSERT_GE(fd, 0);

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_CLONE_FD;
        action.fd.local_fd = fd;
        action.fd.target_fd = 21;

        const char* argv[] = {kSpawnChild, "--action", "clone-fd", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(71, join(process));
        EXPECT_TRUE(has_fd(fd));
        close(fd);
        zx_handle_close(socket);
    }

    {
        zx::socket socket;
        uint32_t type;
        int fd = fdio_pipe_half(socket.reset_and_get_address(), &type);
        ASSERT_GE(fd, 0);

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_TRANSFER_FD;
        action.fd.local_fd = fd;
        action.fd.target_fd = 21;

        const char* argv[] = {kSpawnChild, "--action", "transfer-fd", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(72, join(process));
        EXPECT_FALSE(has_fd(fd));
    }

    {
        zx::socket socket;
        uint32_t type;
        int fd = fdio_pipe_half(socket.reset_and_get_address(), &type);
        ASSERT_GE(fd, 0);

        fdio_spawn_action_t actions[2];
        actions[0].action = FDIO_SPAWN_ACTION_CLONE_FD;
        actions[0].fd.local_fd = fd;
        actions[0].fd.target_fd = 21;
        actions[1].action = FDIO_SPAWN_ACTION_TRANSFER_FD;
        actions[1].fd.local_fd = fd;
        actions[1].fd.target_fd = 22;

        const char* argv[] = {kSpawnChild, "--action", "clone-and-transfer-fd", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 2, actions,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(73, join(process));
        EXPECT_FALSE(has_fd(fd));
    }

    END_TEST;
}

static bool spawn_actions_ns_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        zx::channel h1, h2;
        ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY;
        action.ns.prefix = "/foo/bar/baz";
        action.ns.handle = h1.release();

        const char* argv[] = {kSpawnChild, "--action", "ns-entry", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(74, join(process));
    }

    END_TEST;
}

static bool spawn_actions_h_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        zx::channel h1, h2;
        ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        action.h.id = PA_USER0;
        action.h.handle = h1.release();

        const char* argv[] = {kSpawnChild, "--action", "add-handle", nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(75, join(process));
    }

    END_TEST;
}

static bool spawn_actions_name_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        fdio_spawn_action_t actions[2];
        actions[0].action = FDIO_SPAWN_ACTION_SET_NAME;
        actions[0].name.data = "proc-name-0";
        actions[1].action = FDIO_SPAWN_ACTION_SET_NAME;
        actions[1].name.data = "proc-name-1";

        const char* argv[] = {kSpawnChild, nullptr};
        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 2, actions,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(43, join(process));
        char name[ZX_MAX_NAME_LEN];
        ASSERT_EQ(ZX_OK, process.get_property(ZX_PROP_NAME, name, sizeof(name)));
        EXPECT_EQ(0, strcmp(name, "proc-name-1"));
    }

    END_TEST;
}

static bool spawn_errors_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    const char* argv[] = {kSpawnChild, nullptr};

    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                         nullptr, process.reset_and_get_address()));

    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                             argv, nullptr, 1, nullptr, process.reset_and_get_address(), nullptr));

    {
        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY;
        action.ns.prefix = "/foo/bar/baz";
        action.ns.handle = ZX_HANDLE_INVALID;

        ASSERT_EQ(ZX_ERR_INVALID_ARGS,
                fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                               argv, nullptr, 1, &action, process.reset_and_get_address(), nullptr));
    }

    {
        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        action.h.id = PA_USER0;
        action.h.handle = ZX_HANDLE_INVALID;

        ASSERT_EQ(ZX_ERR_INVALID_ARGS,
                fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                               argv, nullptr, 1, &action, process.reset_and_get_address(), nullptr));
    }

    {
        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_SET_NAME;
        action.name.data = nullptr;

        ASSERT_EQ(ZX_ERR_INVALID_ARGS,
                fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                               argv, nullptr, 1, &action, process.reset_and_get_address(), nullptr));
    }

    {
        const char* empty_argv[] = {nullptr};
        ASSERT_EQ(ZX_ERR_INVALID_ARGS,
                  fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                             empty_argv, process.reset_and_get_address()));
    }

    ASSERT_EQ(ZX_ERR_IO,
              fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/bogus/path",
                             argv, nullptr, 0, nullptr, process.reset_and_get_address(), err_msg));
    EXPECT_TRUE(strstr(err_msg, "/bogus/path") != nullptr);

    {
        zx::job job;
        ASSERT_EQ(ZX_OK, zx::job::default_job()->duplicate(0, &job));
        ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
                  fdio_spawn(job.get(), FDIO_SPAWN_CLONE_ALL, kSpawnChild,
                             argv, process.reset_and_get_address()));
    }

    {
        ASSERT_EQ(30, dup2(0, 30));
        ASSERT_EQ(0, close(0));
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(43, join(process));
        ASSERT_EQ(0, dup2(30, 0));
        ASSERT_EQ(0, close(30));
    }

    {
        zx::channel h1, h2;
        ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

        ASSERT_EQ(30, dup2(0, 30));
        ASSERT_EQ(0, close(0));
        fdio_t* io = fdio_service_create(h1.release());
        ASSERT_EQ(0, fdio_bind_to_fd(io, 0, 0));
        status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                            kSpawnChild, argv, process.reset_and_get_address());
        ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status);
        ASSERT_EQ(0, close(0));
        ASSERT_EQ(0, dup2(30, 0));
        ASSERT_EQ(0, close(30));
    }

    {
        zx::channel h1, h2;
        ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

        fdio_t* io = fdio_service_create(h1.release());
        int fd = fdio_bind_to_fd(io, -1, 0);
        ASSERT_GE(fd, 3);

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_CLONE_FD;
        action.fd.local_fd = fd;
        action.fd.target_fd = 21;

        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action, process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status);
        ASSERT_EQ(0, close(fd));
    }

    {
        zx::channel h1, h2;
        ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

        fdio_t* io = fdio_service_create(h1.release());
        int fd = fdio_bind_to_fd(io, -1, 0);
        ASSERT_GE(fd, 3);

        fdio_spawn_action_t action;
        action.action = FDIO_SPAWN_ACTION_TRANSFER_FD;
        action.fd.local_fd = fd;
        action.fd.target_fd = 21;

        status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                kSpawnChild, argv, nullptr, 1, &action, process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status);
        ASSERT_EQ(-1, close(fd));
    }

    END_TEST;
}

static bool spawn_vmo_test(void) {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;

    {
        int fd = open(kSpawnChild, O_RDONLY);
        ASSERT_GE(fd, 0);
        zx_handle_t vmo;
        ASSERT_EQ(ZX_OK, fdio_get_vmo_clone(fd, &vmo));
        close(fd);

        const char* argv[] = {kSpawnChild, nullptr};
        status = fdio_spawn_vmo(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                vmo, argv, nullptr, 0, nullptr,
                                process.reset_and_get_address(), nullptr);
        ASSERT_EQ(ZX_OK, status);
        EXPECT_EQ(43, join(process));
    }

    END_TEST;
}

BEGIN_TEST_CASE(spawn_tests)
RUN_TEST(spawn_control_test)
RUN_TEST(spawn_launcher_test)
RUN_TEST(spawn_invalid_args_test)
RUN_TEST(spawn_flags_test)
RUN_TEST(spawn_environ_test)
RUN_TEST(spawn_actions_fd_test)
RUN_TEST(spawn_actions_ns_test)
RUN_TEST(spawn_actions_h_test)
RUN_TEST(spawn_actions_name_test)
RUN_TEST(spawn_errors_test)
RUN_TEST(spawn_vmo_test)
END_TEST_CASE(spawn_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}

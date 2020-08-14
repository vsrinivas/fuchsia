// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/fdio/fd.h"
#include "lib/fdio/fdio.h"
#include "lib/fdio/io.h"
#include "lib/fdio/spawn.h"
#include "lib/zx/channel.h"
#include "lib/zx/object.h"
#include "lib/zx/process.h"
#include "lib/zx/socket.h"
#include "zircon/limits.h"
#include "zircon/processargs.h"
#include "zircon/syscalls/object.h"
#include "zircon/syscalls/policy.h"
#include "zircon/types.h"
#include "unittest/unittest.h"


static constexpr char kDash[] = "/pkg/bin/sh";
static constexpr const char* kDashArgv[] = {kDash, nullptr};

static int64_t join(const zx::process& process) {
    zx_status_t status = process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr);
    ASSERT_EQ(ZX_OK, status);
    zx_info_process_t proc_info{};
    status = process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    ASSERT_EQ(ZX_OK, status);
    return proc_info.return_code;
}

static bool dash_ls_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx::process process;
    zx::socket sstdout, sstdin;
    int stdout, stdin;

    status = fdio_pipe_half(&stdout, sstdout.reset_and_get_address());
    ASSERT_GE(status, ZX_OK);
    status = fdio_pipe_half(&stdin, sstdin.reset_and_get_address());
    ASSERT_GE(status, ZX_OK);

    fdio_spawn_action_t actions[3];
    actions[0].action = FDIO_SPAWN_ACTION_CLONE_FD;
    actions[0].fd.local_fd = 2;
    actions[0].fd.target_fd = 2;
    actions[1].action = FDIO_SPAWN_ACTION_TRANSFER_FD;
    actions[1].fd.local_fd = stdout;
    actions[1].fd.target_fd = 1;
    actions[2].action = FDIO_SPAWN_ACTION_TRANSFER_FD;
    actions[2].fd.local_fd = stdin;
    actions[2].fd.target_fd = 0;

    status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_CLONE_NAMESPACE | FDIO_SPAWN_DEFAULT_LDSVC, kDash,
                            kDashArgv, nullptr, 3, actions, process.reset_and_get_address(), nullptr);

    ASSERT_EQ(ZX_OK, status);

    // Note: the PATH=; here forces the call to reach the builtin.
    std::string ls("PATH=; ls /\n");
    ASSERT_EQ(ZX_OK, sstdin.write(0, ls.data(), ls.length(), nullptr));
    sstdin.reset();

    EXPECT_EQ(0, join(process));

    std::string buf(4096, 0);
    size_t actual;
    sstdout.read(0, buf.data(), buf.length(), &actual);
    buf.resize(actual);

    // We don't really care to be hard-coupled to ls output format yet, but we
    // want one line per non-dot dirent.
    std::istringstream output(std::string(buf.begin(), buf.end()));
    int lines = 0;
    for (std::string line; std::getline(output, line);) {
        auto pos = line.rfind(" ");
        if (pos == std::string::npos) {
            continue;
        }
        line = line.substr(pos + 1);
        if (line == "." || line.size() == 0) {
            continue;
        }
        lines++;
    }

    // Check we have something vaguely meaningful
    EXPECT_GT(lines, 1);

    int expected = 0;
    for (__attribute__((unused)) auto& _ : std::filesystem::directory_iterator{"/"}) {
        expected++;
    }

    ASSERT_EQ(lines, expected);

    END_TEST;
}

BEGIN_TEST_CASE(dash_tests)
RUN_TEST(dash_ls_test)
END_TEST_CASE(dash_tests)

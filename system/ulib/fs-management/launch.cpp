// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs-management/mount.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/zx/process.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace {

void InitArgvAndActions(int argc, const char** argv, zx_handle_t* handles,
                        uint32_t* types, size_t len,
                        const char** null_terminated_argv_out,
                        fdio_spawn_action_t* actions_out) {
    for (int i = 0; i < argc; ++i) {
        null_terminated_argv_out[i] = argv[i];
    }
    null_terminated_argv_out[argc] = nullptr;

    for (size_t i = 0; i < len; ++i) {
        actions_out[i].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions_out[i].h.id = types[i];
        actions_out[i].h.handle = handles[i];
    }
}

}  // namespace

zx_status_t launch_logs_async(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len) {
    const char* null_terminated_argv[argc + 1];
    fdio_spawn_action_t actions[len + 1];
    InitArgvAndActions(argc, argv, handles, types, len, null_terminated_argv, actions);

    size_t action_count = len;

    zx_handle_t h;
    zx_log_create(0, &h);
    if (h != ZX_HANDLE_INVALID) {
        actions[action_count].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions[action_count].h.id = PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO);
        actions[action_count].h.handle = h;
        ++action_count;
    }

    uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], null_terminated_argv,
                                        nullptr, action_count, actions, nullptr, err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d (%s): %s\n",
                argv[0], status, zx_status_get_string(status), err_msg);
    }
    return status;
}

zx_status_t launch_stdio_sync(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len) {
    const char* null_terminated_argv[argc + 1];
    fdio_spawn_action_t actions[len];
    InitArgvAndActions(argc, argv, handles, types, len, null_terminated_argv, actions);

    zx::process proc;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                        argv[0], null_terminated_argv, nullptr,
                                        len, actions, proc.reset_and_get_address(),
                                        err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d (%s): %s\n",
                argv[0], status, zx_status_get_string(status), err_msg);
        return status;
    }

    status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
        fprintf(stderr, "launch: Error waiting for process to terminate\n");
        return status;
    }

    zx_info_process_t info;
    status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
        fprintf(stderr, "launch: Failed to get process info\n");
        return status;
    }

    if (!info.exited || info.return_code != 0) {
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t launch_stdio_async(int argc, const char** argv, zx_handle_t* handles,
                               uint32_t* types, size_t len) {
    const char* null_terminated_argv[argc + 1];
    fdio_spawn_action_t actions[len];
    InitArgvAndActions(argc, argv, handles, types, len, null_terminated_argv, actions);

    zx::process proc;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                        argv[0], null_terminated_argv, nullptr,
                                        len, actions, proc.reset_and_get_address(),
                                        err_msg);
    if (status != ZX_OK) {
        fprintf(stderr, "fs-management: Cannot launch %s: %d (%s): %s\n",
                argv[0], status, zx_status_get_string(status), err_msg);
        return status;
    }

    return status;
}

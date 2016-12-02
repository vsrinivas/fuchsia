// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <mxio/util.h>
#include <string.h>

#include "shell.h"
#include "memalloc.h"
#include "nodes.h"
#include "exec.h"
#include "process.h"
#include "options.h"

static mx_handle_t get_mxio_job(void) {
    static mx_handle_t job;
    if (!job)
        job = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_JOB, 0));
    return job;
}

static mx_handle_t get_application_environment(void) {
    static mx_handle_t application_environment;
    if (!application_environment) {
        application_environment =
            mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));
    }
    return application_environment;
}

typedef struct {
    uint32_t header_size;
    uint32_t header_version;
    uint32_t message_ordinal;
    uint32_t message_flags;
    uint32_t message_size;
    uint32_t message_version;
    uint32_t handle;
    uint32_t padding;
} dup_message_t;

static mx_status_t duplicate_application_environment(mx_handle_t application_environment, mx_handle_t* dup_handle) {
    dup_message_t dm;
    dm.header_size = 16;
    dm.header_version = 0;
    dm.message_ordinal = 0; // must match application_environment.fidl
    dm.message_flags = 0;
    dm.message_size = 16;
    dm.message_version = 0;
    dm.handle = 0;
    dm.padding = 0;

    mx_handle_t request_handle;
    mx_status_t status;
    if ((status = mx_channel_create(0, &request_handle, dup_handle)))
        return status;

    if ((status = mx_channel_write(application_environment, 0, &dm, sizeof(dm), &request_handle, 1))) {
        mx_handle_close(request_handle);
        mx_handle_close(*dup_handle);
        *dup_handle = MX_HANDLE_INVALID;
    }
    return status;
}

static mx_status_t clone_application_environment(mx_handle_t* application_environment_for_child) {
    mx_handle_t application_environment = get_application_environment();
    if (application_environment == MX_HANDLE_INVALID) {
        *application_environment_for_child = MX_HANDLE_INVALID;
        return NO_ERROR;
    }
    return duplicate_application_environment(application_environment, application_environment_for_child);
}

static mx_status_t prepare_launch(launchpad_t* lp, const char* filename, int argc, const char* const* argv) {
    mx_status_t status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
    if (status != NO_ERROR)
        return status;

    status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
    if (status != NO_ERROR)
        return status;

    status = launchpad_add_vdso_vmo(lp);
    if (status != NO_ERROR)
        return status;

    status = launchpad_arguments(lp, argc, argv);
    if (status != NO_ERROR)
        return status;

    status = launchpad_environ(lp, (const char* const*)environ);
    if (status != NO_ERROR)
        return status;

    status = launchpad_clone_mxio_root(lp);
    if (status != NO_ERROR)
        return status;

    status = launchpad_clone_mxio_cwd(lp);
    if (status != NO_ERROR)
        return status;

    launchpad_clone_fd(lp, STDIN_FILENO, STDIN_FILENO);
    launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

    mx_handle_t application_environment = MX_HANDLE_INVALID;
    status = clone_application_environment(&application_environment);
    if (status != NO_ERROR)
        return status;

    if (application_environment != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, application_environment,
            MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));
    }

    return status;
}

static mx_status_t launch(const char* filename, int argc, const char* const* argv, mx_handle_t* process) {
    launchpad_t* lp = NULL;

    mx_handle_t job_to_child = MX_HANDLE_INVALID;
    mx_handle_t job = get_mxio_job();
    if (job != MX_HANDLE_INVALID)
        mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_to_child);

    mx_status_t status = launchpad_create(job_to_child, filename, &lp);
    if (status != NO_ERROR)
        return status;

    status = prepare_launch(lp, filename, argc, argv);
    if (status == NO_ERROR) {
        mx_handle_t result = launchpad_start(lp);
        if (result > 0)
            *process = result;
        else
            status = result;
    }

    launchpad_destroy(lp);
    return status;
}

int process_launch(int argc, const char* const* argv, const char* path, int index, mx_handle_t* process) {
    mx_status_t status = NO_ERROR;

    if (strchr(argv[0], '/') != NULL) {
        status = launch(argv[0], argc, argv, process);
        if (status == NO_ERROR)
            return 0;
    } else {
        status = ERR_NOT_FOUND;
        const char* filename = NULL;
        while (status != NO_ERROR && (filename = padvance(&path, argv[0])) != NULL) {
            if (--index < 0 && pathopt == NULL)
                status = launch(filename, argc, argv, process);
            stunalloc(filename);
        }
    }

    switch (status) {
    case NO_ERROR:
        return 0;
    case ERR_ACCESS_DENIED:
        return 126;
    case ERR_NOT_FOUND:
        return 127;
    default:
        return 2;
    }
}

mx_status_t process_subshell(union node* n, mx_handle_t* process) {
    if (!arg0)
        return ERR_NOT_FOUND;

    mx_handle_t ast_vmo = MX_HANDLE_INVALID;
    mx_status_t status = codec_encode(n, &ast_vmo);
    if (status != NO_ERROR)
        return status;

    mx_handle_t application_environment = MX_HANDLE_INVALID;
    status = clone_application_environment(&application_environment);
    if (status != NO_ERROR) {
        mx_handle_close(ast_vmo);
        return status;
    }

    mx_handle_t handles[2];
    uint32_t ids[2];

    size_t count = 0;
    handles[count] = ast_vmo;
    ids[count] = MX_HND_INFO(MX_HND_TYPE_USER0, 0);
    ++count;

    if (application_environment != MX_HANDLE_INVALID) {
        handles[count] = application_environment;
        ids[count] = MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0);
        ++count;
    }

    // TODO(abarth): Handle the redirects properly (i.e., implement
    // redirect(n->nredir.redirect) using launchpad);
    mx_handle_t result = launchpad_launch_mxio_etc(arg0, 1,
        (const char* const*)&arg0, (const char* const*)environ, count, handles, ids);
    if (result < 0)
        return result;
    *process = result;
    return NO_ERROR;
}

int process_await_termination(mx_handle_t process) {
    mx_status_t status = mx_handle_wait_one(process, MX_TASK_TERMINATED, MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR)
        return status;

    mx_info_process_t proc_info;
    status = mx_object_get_info(process, MX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
    if (status != NO_ERROR)
        return status;

    return proc_info.return_code;
}

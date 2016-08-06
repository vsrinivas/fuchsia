// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <mxio/util.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <magenta/processargs.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>

#include <runtime/thread.h>

// 8K is the max io size of the mxio layer right now

static mx_handle_t default_load_object(void* ignored, const char* fn) {
    char buffer[8192];
    char path[PATH_MAX];
    mx_handle_t vmo = 0;
    mx_status_t err = ERR_IO;

    snprintf(path, PATH_MAX, "/boot/lib/%s", fn);

    int fd;
    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(stderr, "dlsvc: could not open '%s'\n", path);
        return ERR_NOT_FOUND;
    }

    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "dlsvc: could not stat '%s'\n", path);
        goto fail;
    }

    if ((vmo = mx_vm_object_create(s.st_size)) < 0) {
        err = vmo;
        goto fail;
    }

    size_t off = 0;
    size_t size = s.st_size;
    ssize_t r;
    while (size > 0) {
        ssize_t xfer = (size > sizeof(buffer)) ? sizeof(buffer) : size;
        if ((r = read(fd, buffer, xfer)) < 0) {
            fprintf(stderr, "dlsvc: read error @%zd in '%s'\n", off, path);
            goto fail;
        }
        if ((r = mx_vm_object_write(vmo, buffer, off, xfer)) != xfer) {
            err = (r < 0) ? r : ERR_IO;
            goto fail;
        }
        off += xfer;
        size -= xfer;
    }
    close(fd);
    return vmo;

fail:
    close(fd);
    mx_handle_close(vmo);
    return err;
}

struct startup {
    mxio_loader_service_function_t loader;
    void* loader_arg;
    mx_handle_t pipe_handle;
};

static int loader_service_thread(void* arg) {
    struct startup* startup = arg;
    mx_handle_t h = startup->pipe_handle;
    mxio_loader_service_function_t loader = startup->loader;
    void* loader_arg = startup->loader_arg;
    free(startup);

    uint8_t data[1024];
    mx_loader_svc_msg_t* msg = (void*) data;
    mx_status_t r;

    for (;;) {
        if ((r = mx_handle_wait_one(h, MX_SIGNAL_READABLE, MX_TIME_INFINITE, NULL)) < 0) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            if (r != ERR_BAD_STATE)
                fprintf(stderr, "dlsvc: wait error %d\n", r);
            break;
        }
        uint32_t sz = sizeof(data);
        if ((r = mx_message_read(h, msg, &sz, NULL, NULL, 0)) < 0) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            if (r != ERR_CHANNEL_CLOSED)
                fprintf(stderr, "dlsvc: msg read error %d\n", r);
            break;
        }
        if ((sz <= sizeof(mx_loader_svc_msg_t))) {
            fprintf(stderr, "dlsvc: runt message\n");
            break;
        }

        // forcibly null-terminate the message data argument
        data[sz - 1] = 0;

        mx_handle_t handle = MX_HANDLE_INVALID;
        switch (msg->opcode) {
        case LOADER_SVC_OP_LOAD_OBJECT:
            handle = (*loader)(loader_arg, (const char*) msg->data);
            msg->arg = handle < 0 ? handle : NO_ERROR;
            break;
        case LOADER_SVC_OP_DEBUG_PRINT:
            fprintf(stderr, "dlsvc: debug: %s\n", (const char*) msg->data);
            msg->arg = NO_ERROR;
            break;
        case LOADER_SVC_OP_DONE:
            goto done;
        default:
            fprintf(stderr, "dlsvc: invalid opcode 0x%x\n", msg->opcode);
            msg->arg = ERR_INVALID_ARGS;
            break;
        }

        msg->opcode = LOADER_SVC_OP_STATUS;
        msg->reserved0 = 0;
        msg->reserved1 = 0;
        if ((r = mx_message_write(h, msg, sizeof(mx_loader_svc_msg_t),
                                  &handle, handle > 0 ? 1 : 0, 0)) < 0) {
            fprintf(stderr, "dlsvc: msg write error: %d\n", r);
            break;
        }
    }

done:
    mx_handle_close(h);
    return 0;
}

mx_handle_t mxio_loader_service(mxio_loader_service_function_t loader,
                                void* loader_arg) {
    if (loader == NULL) {
        loader = &default_load_object;
        loader_arg = NULL;
    }

    struct startup *startup = malloc(sizeof(*startup));
    if (startup == NULL)
        return ERR_NO_MEMORY;

    mx_handle_t h[2];
    mx_status_t r;

    if ((r = mx_message_pipe_create(h, 0)) < 0) {
        free(startup);
        return r;
    }

    startup->pipe_handle = h[1];
    startup->loader = loader;
    startup->loader_arg = loader_arg;

    mxr_thread_t* t;
    if ((r = mxr_thread_create(loader_service_thread, startup,
                               "loader-service", &t)) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        free(startup);
        return r;
    }

    mxr_thread_detach(t);
    return h[0];
}

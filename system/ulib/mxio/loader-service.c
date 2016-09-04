// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxio/util.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>

static void log_printf(mx_handle_t log, const char* fmt, ...) {
    if (log <= 0)
        return;

    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    // we allow partial writes.
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0)
        return;
    len = (len > (int)sizeof(buf)) ? (int)sizeof(buf) : len;
    mx_log_write(log, len, buf, 0u);
}

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

    if ((vmo = mx_vmo_create(s.st_size)) < 0) {
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
        if ((r = mx_vmo_write(vmo, buffer, off, xfer)) != xfer) {
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
    mx_handle_t syslog_handle;
};

static mx_status_t handle_loader_rpc(mx_handle_t h, mxio_loader_service_function_t loader,
                                     void* loader_arg, mx_handle_t sys_log) {
    uint8_t data[1024];
    mx_loader_svc_msg_t* msg = (void*) data;
    uint32_t sz = sizeof(data);
    mx_status_t r;
    if ((r = mx_msgpipe_read(h, msg, &sz, NULL, NULL, 0)) < 0) {
        // This is the normal error for the other end going away,
        // which happens when the process dies.
        if (r != ERR_REMOTE_CLOSED)
            fprintf(stderr, "dlsvc: msg read error %d\n", r);
        return r;
    }
    if ((sz <= sizeof(mx_loader_svc_msg_t))) {
        fprintf(stderr, "dlsvc: runt message\n");
        return ERR_IO;
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
        log_printf(sys_log, "dlsvc: debug: %s\n", (const char*) msg->data);
        msg->arg = NO_ERROR;
        break;
    case LOADER_SVC_OP_DONE:
        return ERR_REMOTE_CLOSED;
    default:
        fprintf(stderr, "dlsvc: invalid opcode 0x%x\n", msg->opcode);
        msg->arg = ERR_INVALID_ARGS;
        break;
    }

    msg->opcode = LOADER_SVC_OP_STATUS;
    msg->reserved0 = 0;
    msg->reserved1 = 0;
    if ((r = mx_msgpipe_write(h, msg, sizeof(mx_loader_svc_msg_t),
                              &handle, handle > 0 ? 1 : 0, 0)) < 0) {
        fprintf(stderr, "dlsvc: msg write error: %d\n", r);
        return r;
    }
    return NO_ERROR;
}

static int loader_service_thread(void* arg) {
    struct startup* startup = arg;
    mx_handle_t h = startup->pipe_handle;
    mxio_loader_service_function_t loader = startup->loader;
    void* loader_arg = startup->loader_arg;
    mx_handle_t sys_log = startup->syslog_handle;
    free(startup);

    mx_status_t r;

    for (;;) {
        if ((r = mx_handle_wait_one(h, MX_SIGNAL_READABLE, MX_TIME_INFINITE, NULL)) < 0) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            if (r != ERR_BAD_STATE)
                fprintf(stderr, "dlsvc: wait error %d\n", r);
            break;
        }
        if ((r = handle_loader_rpc(h, loader, loader_arg, sys_log)) < 0) {
            break;
        }
    }

done:
    mx_handle_close(h);
    return 0;
}


static mx_handle_t dispatcher_log;
static mx_status_t multiloader_cb(mx_handle_t h, void* cb, void* cookie) {
    if (h == 0) {
        // close notification, which we can ignore
        return 0;
    }
    return handle_loader_rpc(h, default_load_object, NULL, dispatcher_log);
}

static mxio_dispatcher_t* dispatcher;
static mtx_t dispatcher_lock;

static mx_handle_t mxio_multiloader(void) {
    mx_status_t r;

    mtx_lock(&dispatcher_lock);
    if (dispatcher == NULL) {
        if ((r = mxio_dispatcher_create(&dispatcher, multiloader_cb)) < 0) {
            goto done;
        }
        if ((r = mxio_dispatcher_start(dispatcher, "loader-service-dispatcher")) < 0) {
            //TODO: destroy dispatcher once support exists
            dispatcher = NULL;
            goto done;
        }
        if ((dispatcher_log = mx_log_create(0)) < 0) {
            // unlikely to fail, but we'll keep going without it if so
            dispatcher_log = MX_HANDLE_INVALID;
        }
    }
    mx_handle_t h[2];
    if ((r = mx_msgpipe_create(h, 0)) < 0) {
        goto done;
    }
    if ((r = mxio_dispatcher_add(dispatcher, h[1], NULL, NULL)) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
    } else {
        r = h[0];
    }

done:
    mtx_unlock(&dispatcher_lock);
    return r;
}

mx_handle_t mxio_loader_service(mxio_loader_service_function_t loader,
                                void* loader_arg) {
    if (loader == NULL) {
        return mxio_multiloader();
    }

    struct startup *startup = malloc(sizeof(*startup));
    if (startup == NULL)
        return ERR_NO_MEMORY;

    mx_handle_t h[2];
    mx_status_t r;

    if ((r = mx_msgpipe_create(h, 0)) < 0) {
        free(startup);
        return r;
    }

    mx_handle_t sys_log = mx_log_create(0u);
    if (sys_log <= 0)
        fprintf(stderr, "dlsvc: log creation failed: error %d\n", sys_log);

    startup->pipe_handle = h[1];
    startup->loader = loader;
    startup->loader_arg = loader_arg;
    startup->syslog_handle = sys_log;

    thrd_t t;
    int ret = thrd_create_with_name(&t, loader_service_thread, startup, "loader-service");
    if (ret != thrd_success) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        free(startup);
        return ret;
    }

    thrd_detach(t);
    return h[0];
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxio/loader-service.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/device/dmctl.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/threads.h>
#include <magenta/types.h>

static void __PRINTFLIKE(2, 3) log_printf(mx_handle_t log,
                                          const char* fmt, ...) {
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

struct mxio_multiloader {
    char name[MX_MAX_NAME_LEN];
    mtx_t dispatcher_lock;
    mxio_dispatcher_t* dispatcher;
    mx_handle_t dispatcher_log;

    char config_prefix[32];
    bool config_exclusive;
};

static const char* const libpaths[] = {
    "/system/lib",
    "/boot/lib",
};

// Always consumes the fd.
static mx_handle_t load_object_fd(int fd, const char* fn) {
    mx_handle_t vmo;
    mx_status_t status = mxio_get_vmo(fd, &vmo);
    close(fd);
    if (status != MX_OK)
        return status;
    mx_object_set_property(vmo, MX_PROP_NAME, fn, strlen(fn));
    return vmo;
}

// For now, just publish data-sink VMOs as files under /tmp/<sink-name>/.
// The individual file is named by its VMO's name.
static mx_status_t publish_data_sink(mx_handle_t vmo, const char* sink_name) {
    union {
        vmo_create_config_t header;
        struct {
            alignas(vmo_create_config_t) char h[sizeof(vmo_create_config_t)];
            char name[MX_MAX_NAME_LEN];
        };
    } config;

    mx_status_t status = mx_object_get_property(
        vmo, MX_PROP_NAME, config.name, sizeof(config.name));
    if (status != MX_OK)
        return status;
    if (config.name[0] == '\0') {
        mx_info_handle_basic_t info;
        status = mx_object_get_info(vmo, MX_INFO_HANDLE_BASIC,
                                    &info, sizeof(info), NULL, NULL);
        if (status != MX_OK)
            return status;
        snprintf(config.name, sizeof(config.name), "unnamed.%" PRIu64,
                 info.koid);
    }

    int tmp_dir_fd = open("/tmp", O_DIRECTORY);
    if (tmp_dir_fd < 0) {
        fprintf(stderr, "dlsvc: cannot open /tmp for data-sink \"%s\": %m\n",
                sink_name);
        close(tmp_dir_fd);
        mx_handle_close(vmo);
        return MX_ERR_NOT_FOUND;
    }
    if (mkdirat(tmp_dir_fd, sink_name, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "dlsvc: cannot mkdir \"/tmp/%s\" for data-sink: %m\n",
                sink_name);
        close(tmp_dir_fd);
        mx_handle_close(vmo);
        return MX_ERR_NOT_FOUND;
    }
    int sink_dir_fd = openat(tmp_dir_fd, sink_name, O_RDONLY | O_DIRECTORY);
    close(tmp_dir_fd);
    if (sink_dir_fd < 0) {
        fprintf(stderr,
                "dlsvc: cannot open data-sink directory \"/tmp/%s\": %m\n",
                sink_name);
        mx_handle_close(vmo);
        return MX_ERR_NOT_FOUND;
    }

    config.header.vmo = vmo;
    ssize_t result = ioctl_vfs_vmo_create(
        sink_dir_fd, &config.header,
        sizeof(config.header) + strlen(config.name) + 1);
    close(sink_dir_fd);

    if (result < 0) {
        fprintf(stderr,
                "dlsvc: ioctl_vfs_vmo_create failed"
                " for data-sink \"%s\" item \"%s\": %s\n",
                sink_name, config.name, mx_status_get_string(result));
        return result;
    }
    return MX_OK;
}

// When loading a library object, search in the hard-coded locations.
static int open_from_libpath(const char* prefix, const char* fn) {
    int fd = -1;
    for (size_t n = 0; fd < 0 && n < countof(libpaths); ++n) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s%s", libpaths[n], prefix, fn);
        fd = open(path, O_RDONLY);
    }
    return fd;
}

static mx_handle_t default_load_object(void* cookie,
                                       uint32_t load_op,
                                       mx_handle_t request_handle,
                                       const char* fn) {
    mxio_multiloader_t* ml = cookie;

    if (request_handle != MX_HANDLE_INVALID &&
        load_op != LOADER_SVC_OP_PUBLISH_DATA_SINK) {
        fprintf(stderr, "dlsvc: unused handle (%#x) opcode=%#x data=\"%s\"\n",
                request_handle, load_op, fn);
        mx_handle_close(request_handle);
    }

    switch (load_op) {
    case LOADER_SVC_OP_CONFIG: {
        size_t len = strlen(fn);
        if (len < 2 || len >= sizeof(ml->config_prefix) - 1 ||
            strchr(fn, '/') != NULL) {
            return MX_ERR_INVALID_ARGS;
        }
        strncpy(ml->config_prefix, fn, len + 1);
        ml->config_exclusive = false;
        if (ml->config_prefix[len - 1] == '!') {
            --len;
            ml->config_exclusive = true;
        }
        ml->config_prefix[len] = '/';
        ml->config_prefix[len + 1] = '\0';
        return MX_OK;
    }
    case LOADER_SVC_OP_LOAD_OBJECT: {
        int fd = -1;
        if (ml->config_prefix[0] != '\0')
            fd = open_from_libpath(ml->config_prefix, fn);
        if (fd < 0 && !ml->config_exclusive)
            fd = open_from_libpath("", fn);
        if (fd >= 0)
            return load_object_fd(fd, fn);
        break;
    }
    case LOADER_SVC_OP_LOAD_SCRIPT_INTERP:
    case LOADER_SVC_OP_LOAD_DEBUG_CONFIG:
        // When loading a script interpreter or debug configuration file,
        // we expect an absolute path.
        if (fn[0] != '/') {
            fprintf(stderr, "dlsvc: invalid %s '%s' is not an absolute path\n",
                    load_op == LOADER_SVC_OP_LOAD_SCRIPT_INTERP ?
                    "script interpreter" : "debug config file",
                    fn);
            return MX_ERR_NOT_FOUND;
        }
        int fd = open(fn, O_RDONLY);
        if (fd >= 0)
            return load_object_fd(fd, fn);
        break;
    case LOADER_SVC_OP_PUBLISH_DATA_SINK:
        return publish_data_sink(request_handle, fn);
    default:
        __builtin_trap();
    }

    fprintf(stderr, "dlsvc: could not open '%s'\n", fn);
    return MX_ERR_NOT_FOUND;
}

struct startup {
    mxio_loader_service_function_t loader;
    void* loader_arg;
    mx_handle_t pipe_handle;
    mx_handle_t syslog_handle;
};

static mx_status_t handle_loader_rpc(mx_handle_t h,
                                     mxio_loader_service_function_t loader,
                                     void* loader_arg, mx_handle_t sys_log) {
    uint8_t data[1024];
    mx_loader_svc_msg_t* msg = (void*) data;
    uint32_t sz = sizeof(data);
    mx_handle_t request_handle;
    uint32_t nhandles;
    mx_status_t r =
        mx_channel_read(h, 0, msg, &request_handle, sz, 1, &sz, &nhandles);
    if (r != MX_OK) {
        // This is the normal error for the other end going away,
        // which happens when the process dies.
        if (r != MX_ERR_PEER_CLOSED)
            fprintf(stderr, "dlsvc: msg read error %d: %s\n", r, mx_status_get_string(r));
        return r;
    }
    if (nhandles == 0)
        request_handle = MX_HANDLE_INVALID;
    if ((sz <= sizeof(mx_loader_svc_msg_t))) {
        mx_handle_close(request_handle);
        fprintf(stderr, "dlsvc: runt message\n");
        return MX_ERR_IO;
    }

    // forcibly null-terminate the message data argument
    data[sz - 1] = 0;

    mx_handle_t handle = MX_HANDLE_INVALID;
    switch (msg->opcode) {
    case LOADER_SVC_OP_CONFIG:
    case LOADER_SVC_OP_LOAD_OBJECT:
    case LOADER_SVC_OP_LOAD_SCRIPT_INTERP:
    case LOADER_SVC_OP_LOAD_DEBUG_CONFIG:
    case LOADER_SVC_OP_PUBLISH_DATA_SINK:
        // TODO(MG-491): Use a threadpool for loading, and guard against
        // other starvation attacks.
        handle = (*loader)(loader_arg, msg->opcode,
                           request_handle, (const char*) msg->data);
        request_handle = MX_HANDLE_INVALID;
        msg->arg = handle < 0 ? handle : MX_OK;
        break;
    case LOADER_SVC_OP_DEBUG_PRINT:
        log_printf(sys_log, "dlsvc: debug: %s\n", (const char*) msg->data);
        msg->arg = MX_OK;
        break;
    case LOADER_SVC_OP_DONE:
        return MX_ERR_PEER_CLOSED;
    default:
        fprintf(stderr, "dlsvc: invalid opcode 0x%x\n", msg->opcode);
        msg->arg = MX_ERR_INVALID_ARGS;
        break;
    }
    if (request_handle != MX_HANDLE_INVALID) {
        fprintf(stderr, "dlsvc: unused handle (%#x) opcode=%#x data=\"%s\"\n",
                request_handle, msg->opcode, msg->data);
        mx_handle_close(request_handle);
    }

    // msg->txid returned as received from the client.
    msg->opcode = LOADER_SVC_OP_STATUS;
    msg->reserved0 = 0;
    msg->reserved1 = 0;
    if ((r = mx_channel_write(h, 0, msg, sizeof(mx_loader_svc_msg_t),
                              &handle, handle > 0 ? 1 : 0)) < 0) {
        fprintf(stderr, "dlsvc: msg write error: %d: %s\n", r, mx_status_get_string(r));
        return r;
    }
    return MX_OK;
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
        if ((r = mx_object_wait_one(h, MX_CHANNEL_READABLE, MX_TIME_INFINITE, NULL)) < 0) {
            // This is the normal error for the other end going away,
            // which happens when the process dies.
            if (r != MX_ERR_BAD_STATE)
                fprintf(stderr, "dlsvc: wait error %d: %s\n", r, mx_status_get_string(r));
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

mx_status_t mxio_multiloader_create(const char* name,
                                    mxio_multiloader_t** ml_out) {
    if (name == NULL || name[0] == '\0' || ml_out == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    mxio_multiloader_t* ml = calloc(1, sizeof(mxio_multiloader_t));
    if (ml == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    strncpy(ml->name, name, sizeof(ml->name) - 1);
    *ml_out = ml;

    return MX_OK;
}

static mx_status_t multiloader_cb(mx_handle_t h, void* cb, void* cookie) {
    if (h == 0) {
        // close notification, which we can ignore
        return 0;
    }
    // This uses ml->dispatcher_log without grabbing the lock, but
    // it will never change once the dispatcher that called us is created.
    mxio_multiloader_t* ml = (mxio_multiloader_t*) cookie;
    return handle_loader_rpc(h, default_load_object, ml, ml->dispatcher_log);
}

// TODO(dbort): Provide a name/id for the process that this handle will
// be used for, to make error messages more useful? Would need to pass
// the same through IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL.
mx_status_t mxio_multiloader_new_service(mxio_multiloader_t* ml, mx_handle_t* out) {
    if (ml == NULL) {
        return MX_ERR_INVALID_ARGS;
    }

    mtx_lock(&ml->dispatcher_lock);
    mx_status_t r;
    if (ml->dispatcher == NULL) {
        if ((r = mxio_dispatcher_create(&ml->dispatcher,
                                        multiloader_cb)) < 0) {
            goto done;
        }
        if ((r = mxio_dispatcher_start(ml->dispatcher, ml->name)) < 0) {
            //TODO: destroy dispatcher once support exists
            ml->dispatcher = NULL;
            goto done;
        }
        if (mx_log_create(0, &ml->dispatcher_log) < 0) {
            // unlikely to fail, but we'll keep going without it if so
            ml->dispatcher_log = MX_HANDLE_INVALID;
        }
    }
    mx_handle_t h0, h1;
    if ((r = mx_channel_create(0, &h0, &h1)) < 0) {
        goto done;
    }
    if ((r = mxio_dispatcher_add(ml->dispatcher, h1, NULL, ml)) < 0) {
        mx_handle_close(h0);
        mx_handle_close(h1);
    } else {
        *out = h0;
    }

done:
    mtx_unlock(&ml->dispatcher_lock);
    return r;
}

static bool force_local_loader_service = false;

void mxio_force_local_loader_service(void) {
    force_local_loader_service = true;
}

// Returns a channel to the system loader service.
static mx_handle_t get_system_loader_service(void) {
    int fd = open("/dev/misc/dmctl", O_RDONLY);
    if (fd < 0) {
        return MX_ERR_NOT_FOUND;
    }

    mx_handle_t h;
    ssize_t s = ioctl_dmctl_get_loader_service_channel(fd, &h);
    close(fd);
    if (s != (ssize_t)sizeof(mx_handle_t)) {
        return s < 0 ? s : MX_ERR_INTERNAL;
    }
    return h;
}

// In-process multiloader
static mxio_multiloader_t local_multiloader = {
    .name = "local-multiloader"
};

mx_status_t mxio_loader_service(mxio_loader_service_function_t loader,
                                void* loader_arg, mx_handle_t* out) {
    if (loader == NULL) {
        if (!force_local_loader_service) {
            // Try to use the system loader service.
            mx_handle_t h = get_system_loader_service();
            if (h > 0) {
                *out = h;
                return MX_OK;
            }
        }
        // Fall back to an in-process loader service.
        return mxio_multiloader_new_service(&local_multiloader, out);
    }
    // Create a loader service using the callback.

    struct startup *startup = malloc(sizeof(*startup));
    if (startup == NULL)
        return MX_ERR_NO_MEMORY;

    mx_handle_t h;
    mx_status_t r;

    if ((r = mx_channel_create(0, &h, &startup->pipe_handle)) < 0) {
        free(startup);
        return r;
    }

    mx_handle_t sys_log = MX_HANDLE_INVALID;
    if ((r = mx_log_create(0u, &sys_log)) < 0)
        fprintf(stderr, "dlsvc: log creation failed: error %d: %s\n", r,
                mx_status_get_string(r));

    startup->loader = loader;
    startup->loader_arg = loader_arg;
    startup->syslog_handle = sys_log;

    thrd_t t;
    int ret = thrd_create_with_name(&t, loader_service_thread, startup,
                                    "local-custom-loader");
    if (ret != thrd_success) {
        mx_handle_close(h);
        mx_handle_close(startup->pipe_handle);
        free(startup);
        return thrd_status_to_mx_status(ret);
    }

    thrd_detach(t);
    *out = h;
    return MX_OK;
}

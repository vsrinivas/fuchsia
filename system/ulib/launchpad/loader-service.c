// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/loader-service.h>

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

#define PREFIX_MAX 32

struct loader_service {
    char name[MX_MAX_NAME_LEN];
    mtx_t dispatcher_lock;
    mxio_dispatcher_t* dispatcher;
    mx_handle_t dispatcher_log;

    const loader_service_ops_t* ops;
    void* ctx;

    char config_prefix[PREFIX_MAX];
    bool config_exclusive;
};

static const char* const libpaths[] = {
    "/system/lib",
    "/boot/lib",
};

mx_status_t loader_service_publish_data_sink_fs(const char* sink_name, mx_handle_t vmo) {
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
static int open_from_libpath(const char* fn) {
    int fd = -1;
    for (size_t n = 0; fd < 0 && n < countof(libpaths); ++n) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", libpaths[n], fn);
        fd = open(path, O_RDONLY);
    }
    return fd;
}

// Always consumes the fd.
static mx_handle_t load_object_fd(int fd, const char* fn, mx_handle_t* out) {
    mx_status_t status = mxio_get_vmo(fd, out);
    close(fd);
    if (status == MX_OK)
        mx_object_set_property(*out, MX_PROP_NAME, fn, strlen(fn));
    return status;
}

static mx_status_t fs_load_object(void *ctx, const char* name, mx_handle_t* out) {
    int fd = open_from_libpath(name);
    if (fd >= 0)
        return load_object_fd(fd, name, out);
    return MX_ERR_NOT_FOUND;
}

static mx_status_t fs_load_abspath(void *ctx, const char* path, mx_handle_t* out) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
        return load_object_fd(fd, path, out);
    return MX_ERR_NOT_FOUND;
}

// For now, just publish data-sink VMOs as files under /tmp/<sink-name>/.
// The individual file is named by its VMO's name.
static mx_status_t fs_publish_data_sink(void* ctx, const char* name, mx_handle_t vmo) {
    return loader_service_publish_data_sink_fs(name, vmo);
}

static const loader_service_ops_t fs_ops = {
    .load_object = fs_load_object,
    .load_abspath = fs_load_abspath,
    .publish_data_sink = fs_publish_data_sink,
};

static mx_status_t default_load_fn(void* cookie, uint32_t load_op,
                                   mx_handle_t request_handle,
                                   const char* fn, mx_handle_t* out) {
    loader_service_t* svc = cookie;
    mx_status_t status;

    switch (load_op) {
    case LOADER_SVC_OP_CONFIG: {
        size_t len = strlen(fn);
        if (len < 2 || len >= sizeof(svc->config_prefix) - 1 ||
            strchr(fn, '/') != NULL) {
            status = MX_ERR_INVALID_ARGS;
            break;
        }
        strncpy(svc->config_prefix, fn, len + 1);
        svc->config_exclusive = false;
        if (svc->config_prefix[len - 1] == '!') {
            --len;
            svc->config_exclusive = true;
        }
        svc->config_prefix[len] = '/';
        svc->config_prefix[len + 1] = '\0';
        status = MX_OK;
        break;
    }
    case LOADER_SVC_OP_LOAD_OBJECT:
        // If a prefix is configured, try loading with that prefix first
        if (svc->config_prefix[0] != '\0') {
            size_t maxlen = PREFIX_MAX + strlen(fn) + 1;
            char pfn[maxlen];
            snprintf(pfn, maxlen, "%s%s", svc->config_prefix, fn);
            if (((status = svc->ops->load_object(svc->ctx, pfn, out)) == MX_OK) ||
                svc->config_exclusive) {
                // if loading with prefix succeeds, or loading
                // with prefix is configured to be exclusive of
                // non-prefix loading, stop here
                break;
            }
            // otherwise, if non-exclusive, try loading without the prefix
        }
        status = svc->ops->load_object(svc->ctx, fn, out);
        break;
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
        status = svc->ops->load_abspath(svc->ctx, fn, out);
        break;
    case LOADER_SVC_OP_PUBLISH_DATA_SINK:
        status = svc->ops->publish_data_sink(svc->ctx, fn, request_handle);
        request_handle = MX_HANDLE_INVALID;
        break;
    case LOADER_SVC_OP_CLONE:
        status = loader_service_attach(svc, request_handle);
        request_handle = MX_HANDLE_INVALID;
        break;
    default:
        __builtin_trap();
    }

    if (request_handle != MX_HANDLE_INVALID) {
        fprintf(stderr, "dlsvc: unused handle (%#x) opcode=%#x data=\"%s\"\n",
                request_handle, load_op, fn);
        mx_handle_close(request_handle);
    }

    return status;
}

struct startup {
    loader_service_fn_t loader;
    void* loader_arg;
    mx_handle_t pipe_handle;
    mx_handle_t syslog_handle;
};

static mx_status_t handle_loader_rpc(mx_handle_t h,
                                     loader_service_fn_t loader,
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
    case LOADER_SVC_OP_CLONE:
        // TODO(MG-491): Use a threadpool for loading, and guard against
        // other starvation attacks.
        r = (*loader)(loader_arg, msg->opcode,
                      request_handle, (const char*) msg->data, &handle);
        if (r == MX_ERR_NOT_FOUND) {
            fprintf(stderr, "dlsvc: could not open '%s'\n",
                    (const char*) msg->data);
        }
        request_handle = MX_HANDLE_INVALID;
        msg->arg = r;
        break;
    case LOADER_SVC_OP_DEBUG_PRINT:
        log_printf(sys_log, "dlsvc: debug: %s\n", (const char*) msg->data);
        msg->arg = MX_OK;
        break;
    case LOADER_SVC_OP_DONE:
        mx_handle_close(request_handle);
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
                              &handle, handle != MX_HANDLE_INVALID ? 1 : 0)) < 0) {
        fprintf(stderr, "dlsvc: msg write error: %d: %s\n", r, mx_status_get_string(r));
        return r;
    }
    return MX_OK;
}

static int loader_service_thread(void* arg) {
    struct startup* startup = arg;
    mx_handle_t h = startup->pipe_handle;
    loader_service_fn_t loader = startup->loader;
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

    mx_handle_close(h);
    return 0;
}

mx_status_t loader_service_create(const char* name,
                                  const loader_service_ops_t* ops,
                                  void* ctx,
                                  loader_service_t** out) {
    if (name == NULL || name[0] == '\0' || out == NULL ||
        ops == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    loader_service_t* svc = calloc(1, sizeof(loader_service_t));
    if (svc == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    svc->ops = ops;
    svc->ctx = ctx;
    strncpy(svc->name, name, sizeof(svc->name) - 1);
    *out = svc;

    return MX_OK;
}

mx_status_t loader_service_create_fs(const char* name,
                                     loader_service_t** out) {
    return loader_service_create(name, &fs_ops, NULL, out);
}

static mx_status_t multiloader_cb(mx_handle_t h, void* cb, void* cookie) {
    if (h == 0) {
        // close notification, which we can ignore
        return 0;
    }
    // This uses svc->dispatcher_log without grabbing the lock, but
    // it will never change once the dispatcher that called us is created.
    loader_service_t* svc = cookie;
    return handle_loader_rpc(h, default_load_fn, svc, svc->dispatcher_log);
}

mx_status_t loader_service_attach(loader_service_t* svc, mx_handle_t h) {
    if (svc == NULL) {
        return MX_ERR_INVALID_ARGS;
    }

    mtx_lock(&svc->dispatcher_lock);
    mx_status_t r;
    if (svc->dispatcher == NULL) {
        if ((r = mxio_dispatcher_create(&svc->dispatcher,
                                        multiloader_cb)) < 0) {
            goto done;
        }
        if ((r = mxio_dispatcher_start(svc->dispatcher, svc->name)) < 0) {
            //TODO: destroy dispatcher once support exists
            svc->dispatcher = NULL;
            goto done;
        }
        if (mx_log_create(0, &svc->dispatcher_log) < 0) {
            // unlikely to fail, but we'll keep going without it if so
            svc->dispatcher_log = MX_HANDLE_INVALID;
        }
    }

    r = mxio_dispatcher_add(svc->dispatcher, h, NULL, svc);

done:
    mtx_unlock(&svc->dispatcher_lock);
    if (r != MX_OK) {
        mx_handle_close(r);
    }
    return r;
}

// TODO(dbort): Provide a name/id for the process that this handle will
// be used for, to make error messages more useful? Would need to pass
// the same through IOCTL_DMCTL_GET_LOADER_SERVICE_CHANNEL.
mx_status_t loader_service_connect(loader_service_t* svc, mx_handle_t* out) {
    mx_handle_t h0, h1;
    mx_status_t r;
    if ((r = mx_channel_create(0, &h0, &h1)) != MX_OK) {
        return r;
    }
    if ((r = loader_service_attach(svc, h1)) != MX_OK) {
        mx_handle_close(h0);
        return r;
    }
    *out = h0;
    return MX_OK;
}

static bool force_local_loader_service = false;

void loader_service_force_local(void) {
    force_local_loader_service = true;
}

// Returns a channel to the system loader service.
mx_status_t loader_service_get_system(mx_handle_t* out) {
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
    *out = h;
    return MX_OK;
}

// In-process multiloader
static loader_service_t local_loader_svc = {
    .name = "local-loader-svc",
    .ops = &fs_ops,
};

mx_status_t loader_service_get_default(mx_handle_t* out) {
    if (!force_local_loader_service) {
        // Try to use the system loader service.
        if (loader_service_get_system(out) == MX_OK) {
            return MX_OK;
        }
    }
    // Fall back to an in-process loader service.
    return loader_service_connect(&local_loader_svc, out);
}

mx_status_t loader_service_simple(loader_service_fn_t loader, void* loader_arg,
                                  mx_handle_t* out) {
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

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <loader-service/loader-service.h>

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/io.h>
#include <inttypes.h>
#include <ldmsg/ldmsg.h>
#include <lib/async-loop/loop.h>
#include <lib/async/wait.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define PREFIX_MAX 32

// State of a loader service instance.
typedef struct instance_state instance_state_t;
struct instance_state {
  int root_dir_fd;
  int data_sink_dir_fd;
  // NULL-terminated list of paths from which objects will loaded.
  const char* const* lib_paths;
};

// This represents an instance of the loader service. Each session in an
// instance has a session_state_t pointing to this. All sessions in
// the same instance behave the same.
struct loader_service {
    atomic_int refcount;
    async_dispatcher_t* dispatcher;

    const loader_service_ops_t* ops;
    void* ctx;
};

// Per-session state of a loader service instance.
typedef struct session_state session_state_t;
struct session_state {
    async_wait_t wait; // Must be first.
    char config_prefix[PREFIX_MAX];
    bool config_exclusive;
    loader_service_t* svc;
};

static void loader_service_addref(loader_service_t* svc) {
    atomic_fetch_add(&svc->refcount, 1);
}

static void loader_service_deref(loader_service_t* svc) {
    if (atomic_fetch_sub(&svc->refcount, 1) == 1) {
        if (svc->ops->finalizer)
            svc->ops->finalizer(svc->ctx);
        free(svc);
    }
}

// When loading a library object, search in the locations provided in
// |lib_paths|, which is required to be NULL-terminated.
static int open_from_lib_paths(int root_dir_fd, const char* const* lib_paths,
                               const char* fn) {
    int fd = -1;
    for (size_t n = 0; fd < 0 && lib_paths[n]; ++n) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", lib_paths[n], fn) < 0) {
            return -1;
        }
        fd = openat(root_dir_fd, path, O_RDONLY);
    }
    return fd;
}

// Always consumes the |fd|.
static zx_handle_t vmo_from_fd(int fd, const char* fn, zx_handle_t* out) {
    zx_status_t status = fdio_get_vmo_clone(fd, out);
    close(fd);
    if (status == ZX_OK) {
        zx_object_set_property(*out, ZX_PROP_NAME, fn, strlen(fn));
    }
    return status;
}

static zx_status_t fd_load_object(void* ctx, const char* name, zx_handle_t* out) {
    int root_dir_fd = ((instance_state_t*)ctx)->root_dir_fd;
    const char* const* lib_paths = ((instance_state_t*)ctx)->lib_paths;

    int fd = open_from_lib_paths(root_dir_fd, lib_paths, name);
    if (fd >= 0) {
        return vmo_from_fd(fd, name, out);
    }
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t fd_load_abspath(void* ctx, const char* path, zx_handle_t* out) {
    int root_dir_fd = ((instance_state_t*)ctx)->root_dir_fd;
    int fd = openat(root_dir_fd, path, O_RDONLY);
    if (fd >= 0) {
        return vmo_from_fd(fd, path, out);
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t fd_publish_data_sink(void* ctx, const char* sink_name, zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

void fd_finalizer(void* ctx) {
    instance_state_t* instance_state = (instance_state_t*)ctx;
    int root_dir_fd = instance_state->root_dir_fd;
    int data_sink_dir_fd = instance_state->data_sink_dir_fd;
    close(root_dir_fd);
    close(data_sink_dir_fd);
    free(instance_state);
}

static const loader_service_ops_t fd_ops = {
    .load_object = fd_load_object,
    .load_abspath = fd_load_abspath,
    .publish_data_sink = fd_publish_data_sink,
    .finalizer = fd_finalizer,
};

static zx_status_t loader_service_rpc(zx_handle_t h, session_state_t* session_state) {
    loader_service_t* svc = session_state->svc;
    ldmsg_req_t req;
    uint32_t req_len = sizeof(req);
    zx_handle_t req_handle = ZX_HANDLE_INVALID;
    uint32_t req_handle_len;
    zx_status_t status =
        zx_channel_read(h, 0, &req, &req_handle, req_len, 1, &req_len, &req_handle_len);
    if (status != ZX_OK) {
        // This is the normal error for the other end going away,
        // which happens when the process dies.
        if (status != ZX_ERR_PEER_CLOSED)
            fprintf(stderr, "dlsvc: msg read error %d: %s\n", status, zx_status_get_string(status));
        return status;
    }

    const char* data = NULL;
    size_t len = 0;
    status = ldmsg_req_decode(&req, req_len, &data, &len);

    if (status != ZX_OK) {
        zx_handle_close(req_handle);
        fprintf(stderr, "dlsvc: invalid message\n");
        return ZX_ERR_IO;
    }

    zx_handle_t rsp_handle = ZX_HANDLE_INVALID;
    switch (req.header.ordinal) {
    case LDMSG_OP_CONFIG: {
        size_t len = strlen(data);
        if (len < 2 || len >= sizeof(session_state->config_prefix) - 1 || strchr(data, '/') != NULL) {
            status = ZX_ERR_INVALID_ARGS;
            break;
        }
        memcpy(session_state->config_prefix, data, len + 1);
        session_state->config_exclusive = false;
        if (session_state->config_prefix[len - 1] == '!') {
            --len;
            session_state->config_exclusive = true;
        }
        session_state->config_prefix[len] = '/';
        session_state->config_prefix[len + 1] = '\0';
        status = ZX_OK;
        break;
    }
    case LDMSG_OP_LOAD_OBJECT:
        // If a prefix is configured, try loading with that prefix first
        if (session_state->config_prefix[0] != '\0') {
            size_t maxlen = PREFIX_MAX + strlen(data) + 1;
            char prefixed_name[maxlen];
            snprintf(prefixed_name, maxlen, "%s%s", session_state->config_prefix, data);
            if (((status = svc->ops->load_object(svc->ctx, prefixed_name, &rsp_handle)) == ZX_OK) ||
                session_state->config_exclusive) {
                // if loading with prefix succeeds, or loading
                // with prefix is configured to be exclusive of
                // non-prefix loading, stop here
                break;
            }
            // otherwise, if non-exclusive, try loading without the prefix
        }
        status = svc->ops->load_object(svc->ctx, data, &rsp_handle);
        break;
    case LDMSG_OP_LOAD_SCRIPT_INTERPRETER:
    case LDMSG_OP_DEBUG_LOAD_CONFIG:
        // When loading a script interpreter or debug configuration file,
        // we expect an absolute path.
        if (data[0] != '/') {
            fprintf(stderr, "dlsvc: invalid %s '%s' is not an absolute path\n",
                    req.header.ordinal == LDMSG_OP_LOAD_SCRIPT_INTERPRETER ? "script interpreter" : "debug config file",
                    data);
            status = ZX_ERR_NOT_FOUND;
            break;
        }
        status = svc->ops->load_abspath(svc->ctx, data, &rsp_handle);
        break;
    case LDMSG_OP_DEBUG_PUBLISH_DATA_SINK:
        status = svc->ops->publish_data_sink(svc->ctx, data, req_handle);
        req_handle = ZX_HANDLE_INVALID;
        break;
    case LDMSG_OP_CLONE:
        status = loader_service_attach(svc, req_handle);
        req_handle = ZX_HANDLE_INVALID;
        break;
    case LDMSG_OP_DONE:
        zx_handle_close(req_handle);
        return ZX_ERR_PEER_CLOSED;
    default:
        // This case cannot happen because ldmsg_req_decode will return an
        // error for invalid ordinals.
        __builtin_trap();
    }

    if (status == ZX_ERR_NOT_FOUND) {
        fprintf(stderr, "dlsvc: could not open '%s'\n", data);
    }

    if (req_handle != ZX_HANDLE_INVALID) {
        fprintf(stderr, "dlsvc: unused handle (%#x) opcode=%#x data=\"%s\"\n",
                req_handle, req.header.ordinal, data);
        zx_handle_close(req_handle);
    }

    ldmsg_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.header.txid = req.header.txid;
    rsp.header.ordinal = req.header.ordinal;
    rsp.rv = status;
    rsp.object = rsp_handle == ZX_HANDLE_INVALID ? FIDL_HANDLE_ABSENT : FIDL_HANDLE_PRESENT;
    if ((status = zx_channel_write(h, 0, &rsp, ldmsg_rsp_get_size(&rsp),
                                   &rsp_handle, rsp_handle != ZX_HANDLE_INVALID ? 1 : 0)) < 0) {
        fprintf(stderr, "dlsvc: msg write error: %d: %s\n", status, zx_status_get_string(status));
        return status;
    }
    return ZX_OK;
}

zx_status_t loader_service_create(async_dispatcher_t* dispatcher,
                                  const loader_service_ops_t* ops,
                                  void* ctx,
                                  loader_service_t** out) {
    if (out == NULL || ops == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    loader_service_t* svc = calloc(1, sizeof(loader_service_t));
    if (svc == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    if (!dispatcher) {
        async_loop_t* loop;
        zx_status_t status = async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop);
        if (status != ZX_OK) {
            free(svc);
            return status;
        }

        status = async_loop_start_thread(loop, "loader-service", NULL);
        if (status != ZX_OK) {
            free(svc);
            async_loop_destroy(loop);
            return status;
        }

        dispatcher = async_loop_get_dispatcher(loop);
    }

    svc->dispatcher = dispatcher;
    svc->ops = ops;
    svc->ctx = ctx;

    // When we create the loader service, we initialize the refcount to 1, which
    // causes the loader service to stay alive at least until someone calls
    // |loader_service_release|, at which point the service will be destroyed
    // once the last client goes away.
    loader_service_addref(svc);

    *out = svc;
    return ZX_OK;
}

// Default library paths for the fd- and fs- loader service implementations.
static const char* const fd_lib_paths[] = {"lib", NULL};
static const char* const fs_lib_paths[] = {"system/lib", "boot/lib", NULL};

// Create the default implementation of a loader service for which
// paths are loaded relative to |root_dir_fd| and among the array of
// subdirectories given by |lib_paths| (NULL-terminated), with data published
// in the location given by |data_sink_dir_fd|.
zx_status_t loader_service_create_default(async_dispatcher_t* dispatcher,
                                          int root_dir_fd,
                                          int data_sink_dir_fd,
                                          const char* const* lib_paths,
                                          loader_service_t** out) {
    instance_state_t* instance_state = calloc(1, sizeof(loader_service_t));
    if (instance_state == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    instance_state->root_dir_fd = root_dir_fd;
    instance_state->data_sink_dir_fd = data_sink_dir_fd;
    instance_state->lib_paths = lib_paths? lib_paths : fd_lib_paths;

    loader_service_t* svc;
    zx_status_t status = loader_service_create(dispatcher, &fd_ops, NULL, &svc);
    if (status == ZX_OK) {
      svc->ctx = instance_state;
      *out = svc;
    } else {
      free(instance_state);
    }
    return status;
}

zx_status_t loader_service_create_fs(async_dispatcher_t* dispatcher, loader_service_t** out) {
    int root_dir_fd = open("/", O_RDONLY | O_DIRECTORY);
    if (root_dir_fd < 0){
      return ZX_ERR_NOT_FOUND;
    }
    return loader_service_create_default(dispatcher, root_dir_fd, -1, fs_lib_paths,
                                         out);
}

zx_status_t loader_service_create_fd(async_dispatcher_t* dispatcher,
                                     int root_dir_fd,
                                     int data_sink_dir_fd,
                                     loader_service_t** out) {
    return loader_service_create_default(dispatcher, root_dir_fd, data_sink_dir_fd,
                                         fd_lib_paths, out);
}

zx_status_t loader_service_release(loader_service_t* svc) {
    // This call to |loader_service_deref| balances the |loader_service_addref|
    // call in |loader_service_create|. This reference prevents the loader
    // service from being destroyed before its creator is done with it.
    loader_service_deref(svc);
    return ZX_OK;
}

static void loader_service_handler(async_dispatcher_t* dispatcher,
                                   async_wait_t* wait,
                                   zx_status_t status,
                                   const zx_packet_signal_t* signal) {
    session_state_t* session_state = (session_state_t*)wait;
    if (status != ZX_OK)
        goto stop;
    status = loader_service_rpc(wait->object, session_state);
    if (status != ZX_OK)
        goto stop;
    status = async_begin_wait(dispatcher, wait);
    if (status != ZX_OK)
        goto stop;
    return;
stop:
    zx_handle_close(wait->object);
    loader_service_t* svc = session_state->svc;
    free(session_state);
    loader_service_deref(svc); // Balanced in |loader_service_attach|.
}

zx_status_t loader_service_attach(loader_service_t* svc, zx_handle_t h) {
    zx_status_t status = ZX_OK;
    session_state_t* session_state = NULL;

    if (svc == NULL) {
        status = ZX_ERR_INVALID_ARGS;
        goto done;
    }

    session_state = calloc(1, sizeof(session_state_t));
    if (session_state == NULL) {
        status = ZX_ERR_NO_MEMORY;
        goto done;
    }

    session_state->wait.handler = loader_service_handler;
    session_state->wait.object = h;
    session_state->wait.trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    session_state->svc = svc;

    status = async_begin_wait(svc->dispatcher, &session_state->wait);

    if (status == ZX_OK) {
        loader_service_addref(svc); // Balanced in |loader_service_handler|.
    }

done:
    if (status != ZX_OK) {
        zx_handle_close(h);
        free(session_state);
    }
    return status;
}

zx_status_t loader_service_connect(loader_service_t* svc, zx_handle_t* out) {
    zx_handle_t h0, h1;
    zx_status_t status;
    if ((status = zx_channel_create(0, &h0, &h1)) != ZX_OK) {
        return status;
    }
    if ((status = loader_service_attach(svc, h1)) != ZX_OK) {
        zx_handle_close(h0);
        return status;
    }
    *out = h0;
    return ZX_OK;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>

#include <fcntl.h>
#include <fuchsia/process/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "private.h"

#define FDIO_RESOLVE_PREFIX "#!resolve "
#define FDIO_RESOLVE_PREFIX_LEN 10

// It is possible to setup an infinite loop of resolvers. We want to avoid this
// being a common abuse vector, but also stay out of the way of any complex user
// setups.
#define FDIO_SPAWN_MAX_RESOLVE_DEPTH 255

#define FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE ((size_t)0u)
#define FDIO_SPAWN_LAUNCH_HANDLE_JOB ((size_t)1u)
#define FDIO_SPAWN_LAUNCH_HANDLE_COUNT ((size_t)2u)

#define FDIO_SPAWN_LAUNCH_REPLY_HANDLE_COUNT ((size_t)1u)

// The fdio_spawn_action_t is replicated in various ffi interfaces, including
// the rust and golang standard libraries.
static_assert(sizeof(fdio_spawn_action_t) == 24,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, action) == 0,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd.local_fd) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, fd.target_fd) == 12,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns.prefix) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, ns.handle) == 16,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h.id) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, h.handle) == 12,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, name) == 8,
              "fdio_spawn_action_t must have a stable ABI");
static_assert(offsetof(fdio_spawn_action_t, name.data) == 8,
              "fdio_spawn_action_t must have a stable ABI");

static zx_status_t load_path(const char* path, zx_handle_t* vmo) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return ZX_ERR_NOT_FOUND;

    zx_status_t status = fdio_get_vmo_clone(fd, vmo);
    close(fd);

    if (status == ZX_OK) {
        if (strlen(path) >= ZX_MAX_NAME_LEN) {
            const char* p = strrchr(path, '/');
            if (p != NULL) {
                path = p + 1;
            }
        }

        zx_object_set_property(*vmo, ZX_PROP_NAME, path, strlen(path));
    }

    return status;
}

static void measure_cstring_array(const char* const* array, size_t* count_out, size_t* len_out) {
    size_t i = 0;
    size_t len = 0;
    while (array[i]) {
        len += FIDL_ALIGN(strlen(array[i]));
        ++i;
    }
    *count_out = i;
    *len_out = len;
}

static void report_error(char* err_msg, const char* format, ...) {
    if (!err_msg)
        return;
    va_list args;
    va_start(args, format);
    vsnprintf(err_msg, FDIO_SPAWN_ERR_MSG_MAX_LENGTH, format, args);
    va_end(args);
}

// resolve_name makes a call to the fuchsia.process.Resolver service and may
// return a vmo and associated loader service, if the name resolves within the
// current realm.
static zx_status_t resolve_name(const char* name, size_t name_len,
                                zx_handle_t* vmo, zx_handle_t* ldsvc,
                                char* err_msg) {
    zx_handle_t resolver, resolver_request;
    zx_status_t status = zx_channel_create(0, &resolver, &resolver_request);
    if (status != ZX_OK) {
        report_error(err_msg, "failed to create channel: %d", status);
        return ZX_ERR_INTERNAL;
    }

    status = fdio_service_connect("/svc/fuchsia.process.Resolver", resolver_request);
    resolver_request = ZX_HANDLE_INVALID;
    if (status != ZX_OK) {
        zx_handle_close(resolver);
        report_error(err_msg, "failed to connect to resolver service: %d", status);
        return ZX_ERR_INTERNAL;
    }

    zx_status_t io_status = fuchsia_process_ResolverResolve(
        resolver, name, name_len, &status, vmo, ldsvc);
    zx_handle_close(resolver);
    if (io_status != ZX_OK) {
        report_error(err_msg, "failed to send resolver request: %d", io_status);
        return ZX_ERR_INTERNAL;
    }

    if (status != ZX_OK) {
        report_error(err_msg, "failed to resolve %.*s", name_len, name);
    }
    return status;
}

static zx_status_t send_cstring_array(zx_handle_t launcher, int ordinal, const char* const* array) {
    size_t count = 0;
    size_t len = 0;

    // TODO(abarth): In principle, we should chunk array into separate
    // messages if we exceed ZX_CHANNEL_MAX_MSG_BYTES.
    measure_cstring_array(array, &count, &len);

    if (count == 0)
        return ZX_OK;

    size_t msg_len = sizeof(fidl_message_header_t) + sizeof(fidl_vector_t) + count * sizeof(fidl_vector_t) + FIDL_ALIGN(len);
    uint8_t msg[msg_len];
    memset(msg, 0, msg_len);

    fidl_message_header_t* hdr = (fidl_message_header_t*)msg;
    fidl_vector_t* vector = (fidl_vector_t*)hdr + 1;
    fidl_vector_t* bytes = (fidl_vector_t*)(vector + 1);
    uint8_t* payload = (uint8_t*)(bytes + count);

    hdr->ordinal = ordinal;
    vector->count = count;
    vector->data = (void*)FIDL_ALLOC_PRESENT;

    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t size = strlen(array[i]);
        bytes[i].count = size;
        bytes[i].data = (void*)FIDL_ALLOC_PRESENT;
        memcpy(payload + offset, array[i], size);
        offset += FIDL_ALIGN(size);
    }

    return zx_channel_write(launcher, 0, msg, msg_len, NULL, 0);
}

static zx_status_t send_handles(zx_handle_t launcher, size_t handle_capacity,
                                uint32_t flags, zx_handle_t job,
                                zx_handle_t ldsvc, size_t action_count,
                                const fdio_spawn_action_t* actions, char* err_msg) {
    // TODO(abarth): In principle, we should chunk array into separate
    // messages if we exceed ZX_CHANNEL_MAX_MSG_HANDLES.

    size_t msg_capacity = sizeof(fuchsia_process_LauncherAddHandlesRequest) + FIDL_ALIGN(handle_capacity * sizeof(fuchsia_process_HandleInfo));
    uint8_t msg[msg_capacity];
    memset(msg, 0, msg_capacity);

    fuchsia_process_LauncherAddHandlesRequest* req = (fuchsia_process_LauncherAddHandlesRequest*)msg;
    fuchsia_process_HandleInfo* handle_infos = (fuchsia_process_HandleInfo*)(req + 1);

    zx_handle_t handles[handle_capacity];

    memset(handles, 0, sizeof(handles));

    req->hdr.ordinal = fuchsia_process_LauncherAddHandlesOrdinal;

    zx_status_t status = ZX_OK;
    size_t h = 0;
    size_t a = 0;

    if ((flags & FDIO_SPAWN_CLONE_JOB) != 0) {
        handle_infos[h].handle = FIDL_HANDLE_PRESENT;
        handle_infos[h].id = PA_JOB_DEFAULT;
        status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &handles[h++]);
        if (status != ZX_OK) {
            report_error(err_msg, "failed to duplicate job: %d", status);
            goto cleanup;
        }
    }

    if ((flags & FDIO_SPAWN_DEFAULT_LDSVC) != 0) {
        handle_infos[h].handle = FIDL_HANDLE_PRESENT;
        handle_infos[h].id = PA_LDSVC_LOADER;
        if (ldsvc == ZX_HANDLE_INVALID) {
            status = dl_clone_loader_service(&ldsvc);
            if (status != ZX_OK) {
                report_error(err_msg, "failed to clone library loader service: %d", status);
                goto cleanup;
            }
        }
        handles[h++] = ldsvc;
        ldsvc = ZX_HANDLE_INVALID;
    } else if (ldsvc != ZX_HANDLE_INVALID) {
        zx_handle_close(ldsvc);
        ldsvc = ZX_HANDLE_INVALID;
    }

    if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0) {
        for (int fd = 0; fd < 3; ++fd) {
            zx_handle_t fd_handle = ZX_HANDLE_INVALID;
            status = fdio_fd_clone(fd, &fd_handle);
            if (status == ZX_ERR_INVALID_ARGS) {
                // This file descriptor is closed. We just skip it rather than
                // generating an error.
                continue;
            }
            if (status != ZX_OK) {
                report_error(err_msg, "failed to clone fd %d: %d", fd, status);
                goto cleanup;
            }
            handle_infos[h].handle = FIDL_HANDLE_PRESENT;
            handle_infos[h].id = PA_HND(PA_FD, fd);
            handles[h++] = fd_handle;
        }
    }

    for (; a < action_count; ++a) {
        zx_handle_t fd_handle = ZX_HANDLE_INVALID;

        switch (actions[a].action) {
        case FDIO_SPAWN_ACTION_CLONE_FD:
            status = fdio_fd_clone(actions[a].fd.local_fd, &fd_handle);
            if (status != ZX_OK) {
                report_error(err_msg, "failed to clone fd %d (action index %zu): %d", actions[a].fd.local_fd, a, status);
                goto cleanup;
            }
            break;
        case FDIO_SPAWN_ACTION_TRANSFER_FD:
            status = fdio_fd_transfer(actions[a].fd.local_fd, &fd_handle);
            if (status != ZX_OK) {
                report_error(err_msg, "failed to transfer fd %d (action index %zu): %d", actions[a].fd.local_fd, a, status);
                goto cleanup;
            }
            break;
        case FDIO_SPAWN_ACTION_ADD_HANDLE:
            handle_infos[h].handle = FIDL_HANDLE_PRESENT;
            handle_infos[h].id = actions[a].h.id;
            handles[h++] = actions[a].h.handle;
            continue;
        default:
            continue;
        }

        handle_infos[h].handle = FIDL_HANDLE_PRESENT;
        handle_infos[h].id = PA_HND(PA_FD, actions[a].fd.target_fd);
        handles[h++] = fd_handle;
    }

    req->handles.count = h;
    req->handles.data = (void*)FIDL_ALLOC_PRESENT;

    ZX_DEBUG_ASSERT(h <= handle_capacity);

    size_t msg_len = sizeof(fuchsia_process_LauncherAddHandlesRequest) + FIDL_ALIGN(h * sizeof(fuchsia_process_HandleInfo));
    status = zx_channel_write(launcher, 0, msg, msg_len, handles, h);

    if (status != ZX_OK)
        report_error(err_msg, "failed send handles: %d", status);

    return status;

cleanup:
    if (ldsvc != ZX_HANDLE_INVALID)
        zx_handle_close(ldsvc);

    zx_handle_close_many(handles, h);

    // If |a| is less than |action_count|, that means we encountered an error
    // before we processed all the actions. We need to iterate through the rest
    // of the table and close the file descriptors and handles that we're
    // supposed to consume.
    for (size_t i = a; i < action_count; ++i) {
        switch (actions[i].action) {
        case FDIO_SPAWN_ACTION_TRANSFER_FD:
            close(actions[i].fd.local_fd);
            break;
        case FDIO_SPAWN_ACTION_ADD_HANDLE:
            zx_handle_close(actions[i].h.handle);
            break;
        }
    }

    return status;
}

static zx_status_t send_namespace(zx_handle_t launcher, size_t name_count, size_t name_len,
                                  fdio_flat_namespace_t* flat, size_t action_count,
                                  const fdio_spawn_action_t* actions, char* err_msg) {
    size_t msg_len = sizeof(fuchsia_process_LauncherAddNamesRequest) + FIDL_ALIGN(name_count * sizeof(fuchsia_process_NameInfo)) + FIDL_ALIGN(name_len);
    uint8_t msg[msg_len];
    memset(msg, 0, msg_len);

    fuchsia_process_LauncherAddNamesRequest* req = (fuchsia_process_LauncherAddNamesRequest*)msg;
    fuchsia_process_NameInfo* names = (fuchsia_process_NameInfo*)(req + 1);
    uint8_t* payload = (uint8_t*)(names + name_count);

    zx_handle_t handles[name_count];

    memset(handles, 0, sizeof(handles));

    req->hdr.ordinal = fuchsia_process_LauncherAddNamesOrdinal;
    req->names.count = name_count;
    req->names.data = (void*)FIDL_ALLOC_PRESENT;

    size_t n = 0;
    size_t h = 0;
    size_t offset = 0;

    if (flat) {
        while (n < flat->count) {
            size_t size = strlen(flat->path[n]);
            names[n].path.size = size;
            names[n].path.data = (void*)FIDL_ALLOC_PRESENT;
            names[n].directory = FIDL_HANDLE_PRESENT;
            memcpy(payload + offset, flat->path[n], size);
            offset += FIDL_ALIGN(size);
            handles[h++] = flat->handle[n];
            n++;
        }
    }

    for (size_t i = 0; i < action_count; ++i) {
        if (actions[i].action == FDIO_SPAWN_ACTION_ADD_NS_ENTRY) {
            size_t size = strlen(actions[i].ns.prefix);
            names[n].path.size = size;
            names[n].path.data = (void*)FIDL_ALLOC_PRESENT;
            names[n].directory = FIDL_HANDLE_PRESENT;
            memcpy(payload + offset, actions[i].ns.prefix, size);
            offset += FIDL_ALIGN(size);
            handles[h++] = actions[i].ns.handle;
            n++;
        }
    }

    ZX_DEBUG_ASSERT(n == name_count);
    ZX_DEBUG_ASSERT(h == name_count);

    zx_status_t status = zx_channel_write(launcher, 0, msg, msg_len, handles, h);

    if (status != ZX_OK)
        report_error(err_msg, "failed send namespace: %d", status);

    return status;
}

__EXPORT
zx_status_t fdio_spawn(zx_handle_t job,
                       uint32_t flags,
                       const char* path,
                       const char* const* argv,
                       zx_handle_t* process_out) {
    return fdio_spawn_etc(job, flags, path, argv, NULL, 0, NULL, process_out, NULL);
}

__EXPORT
zx_status_t fdio_spawn_etc(zx_handle_t job,
                           uint32_t flags,
                           const char* path,
                           const char* const* argv,
                           const char* const* explicit_environ,
                           size_t action_count,
                           const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out,
                           char* err_msg) {
    zx_handle_t executable_vmo = ZX_HANDLE_INVALID;

    zx_status_t status = load_path(path, &executable_vmo);

    if (status != ZX_OK) {
        report_error(err_msg, "failed to load executable from %s", path);
        // Set |err_msg| to NULL to prevent |fdio_spawn_vmo| from generating
        // a less useful error message.
        err_msg = NULL;
    }

    // Always call fdio_spawn_vmo to clean up arguments. If |executable_vmo| is
    // |ZX_HANDLE_INVALID|, then |fdio_spawn_vmo| will generate an error.
    zx_status_t spawn_status = fdio_spawn_vmo(job, flags, executable_vmo, argv, explicit_environ,
                                              action_count, actions, process_out, err_msg);

    // Use |status| if we already had an error before calling |fdio_spawn_vmo|.
    // Otherwise, we'll always return |ZX_ERR_INVALID_ARGS| rather than the more
    // useful status from |load_path|.
    return status != ZX_OK ? status : spawn_status;
}

__EXPORT
zx_status_t fdio_spawn_vmo(zx_handle_t job,
                           uint32_t flags,
                           zx_handle_t executable_vmo,
                           const char* const* argv,
                           const char* const* explicit_environ,
                           size_t action_count,
                           const fdio_spawn_action_t* actions,
                           zx_handle_t* process_out,
                           char* err_msg) {
    zx_status_t status = ZX_OK;
    fdio_flat_namespace_t* flat = NULL;
    size_t name_count = 0;
    size_t name_len = 0;
    size_t handle_capacity = 0;
    zx_handle_t launcher = ZX_HANDLE_INVALID;
    zx_handle_t launcher_request = ZX_HANDLE_INVALID;
    zx_handle_t msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_COUNT];
    zx_handle_t ldsvc = ZX_HANDLE_INVALID;

    memset(msg_handles, 0, sizeof(msg_handles));

    if (err_msg)
        err_msg[0] = '\0';

    // We intentionally don't fill in |err_msg| for invalid args.

    if (executable_vmo == ZX_HANDLE_INVALID || !argv || (action_count != 0 && !actions)) {
        status = ZX_ERR_INVALID_ARGS;
        goto cleanup;
    }

    if (job == ZX_HANDLE_INVALID)
        job = zx_job_default();

    const char* process_name = argv[0];

    for (size_t i = 0; i < action_count; ++i) {
        switch (actions[i].action) {
        case FDIO_SPAWN_ACTION_CLONE_FD:
        case FDIO_SPAWN_ACTION_TRANSFER_FD:
            ++handle_capacity;
            break;
        case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
            if (actions[i].ns.handle == ZX_HANDLE_INVALID || !actions[i].ns.prefix) {
                status = ZX_ERR_INVALID_ARGS;
                goto cleanup;
            }
            ++name_count;
            name_len += FIDL_ALIGN(strlen(actions[i].ns.prefix));
            break;
        case FDIO_SPAWN_ACTION_ADD_HANDLE:
            if (actions[i].h.handle == ZX_HANDLE_INVALID) {
                status = ZX_ERR_INVALID_ARGS;
                goto cleanup;
            }
            ++handle_capacity;
            break;
        case FDIO_SPAWN_ACTION_SET_NAME:
            if (actions[i].name.data == NULL) {
                status = ZX_ERR_INVALID_ARGS;
                goto cleanup;
            }
            process_name = actions[i].name.data;
            break;
        default:
            break;
        }
    }

    if (!process_name) {
        status = ZX_ERR_INVALID_ARGS;
        goto cleanup;
    }

    if ((flags & FDIO_SPAWN_CLONE_JOB) != 0)
        ++handle_capacity;

    if ((flags & FDIO_SPAWN_DEFAULT_LDSVC) != 0)
        ++handle_capacity;

    if ((flags & FDIO_SPAWN_CLONE_STDIO) != 0)
        handle_capacity += 3;

    if ((flags & FDIO_SPAWN_CLONE_NAMESPACE) != 0) {
        status = fdio_ns_export_root(&flat);
        name_count += flat->count;
        for (size_t i = 0; i < flat->count; ++i) {
            name_len += FIDL_ALIGN(strlen(flat->path[i]));
        }
    }

    // resolve vmos containing #!resolve, updating the vmo & ldsvc
    for (size_t i = 0; true; ++i) {
        char head[fuchsia_process_MAX_RESOLVE_NAME_SIZE + FDIO_RESOLVE_PREFIX_LEN];
        ZX_ASSERT(sizeof(head) < PAGE_SIZE);
        memset(head, 0, sizeof(head));
        status = zx_vmo_read(executable_vmo, head, 0, sizeof(head));
        if (status != ZX_OK) {
            report_error(err_msg, "error reading executable vmo: %d", status);
            goto cleanup;
        }
        if (memcmp(FDIO_RESOLVE_PREFIX, head, FDIO_RESOLVE_PREFIX_LEN) != 0) {
            break;
        }

        // resolves are not allowed to carry on forever.
        if (i == FDIO_SPAWN_MAX_RESOLVE_DEPTH) {
            status = ZX_ERR_IO_INVALID;
            report_error(err_msg, "hit recursion limit resolving name");
            goto cleanup;
        }

        char* name = &head[FDIO_RESOLVE_PREFIX_LEN];
        size_t len = fuchsia_process_MAX_RESOLVE_NAME_SIZE;
        char* end = memchr(name, '\n', len);
        if (end != NULL) {
            len = end - name;
        }

        status = resolve_name(name, len, &executable_vmo, &ldsvc, err_msg);
        if (status != ZX_OK) {
            goto cleanup;
        }
    }

    status = zx_channel_create(0, &launcher, &launcher_request);
    if (status != ZX_OK) {
        report_error(err_msg, "failed to create channel for process launcher: %d", status);
        goto cleanup;
    }

    status = fdio_service_connect("/svc/fuchsia.process.Launcher", launcher_request);
    launcher_request = ZX_HANDLE_INVALID;
    if (status != ZX_OK) {
        report_error(err_msg, "failed to connect to launcher service: %d", status);
        goto cleanup;
    }

    status = send_cstring_array(launcher, fuchsia_process_LauncherAddArgsOrdinal, argv);
    if (status != ZX_OK) {
        report_error(err_msg, "failed to send argument vector: %d", status);
        goto cleanup;
    }

    if (explicit_environ) {
        status = send_cstring_array(launcher, fuchsia_process_LauncherAddEnvironsOrdinal, explicit_environ);
        if (status != ZX_OK) {
            report_error(err_msg, "failed to send environment: %d", status);
            goto cleanup;
        }
    } else if ((flags & FDIO_SPAWN_CLONE_ENVIRON) != 0) {
        status = send_cstring_array(launcher, fuchsia_process_LauncherAddEnvironsOrdinal, (const char* const*)environ);
        if (status != ZX_OK) {
            report_error(err_msg, "failed to send environment clone with FDIO_SPAWN_CLONE_ENVIRON: %d", status);
            goto cleanup;
        }
    }

    if (handle_capacity) {
        status = send_handles(launcher, handle_capacity, flags, job, ldsvc, action_count, actions, err_msg);
        ldsvc = ZX_HANDLE_INVALID;
        if (status != ZX_OK) {
            // When |send_handles| fails, it consumes all the action handles
            // that it knows about, but it doesn't consume the handles used for
            // |FDIO_SPAWN_ACTION_ADD_NS_ENTRY|.

            for (size_t i = 0; i < action_count; ++i) {
                switch (actions[i].action) {
                case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
                    zx_handle_close(actions[i].ns.handle);
                    break;
                default:
                    break;
                }
            }

            action_count = 0; // We've now consumed all the handles.
            goto cleanup;
        }
    }

    if (name_count) {
        status = send_namespace(launcher, name_count, name_len, flat, action_count, actions, err_msg);
        if (status != ZX_OK) {
            action_count = 0;
            goto cleanup;
        }
    }

    action_count = 0; // We've consumed all the actions at this point.

    size_t process_name_size = strlen(process_name);
    if (process_name_size >= ZX_MAX_NAME_LEN)
        process_name_size = ZX_MAX_NAME_LEN - 1;

    {

        struct {
            FIDL_ALIGNDECL
            fuchsia_process_LauncherLaunchRequest req;
            uint8_t process_name[FIDL_ALIGN(ZX_MAX_NAME_LEN)];
        } msg;

        memset(&msg, 0, sizeof(msg));
        size_t msg_len = sizeof(fuchsia_process_LauncherLaunchRequest) + FIDL_ALIGN(process_name_size);

        msg.req.hdr.ordinal = fuchsia_process_LauncherLaunchOrdinal;
        msg.req.info.executable = FIDL_HANDLE_PRESENT;
        msg.req.info.job = FIDL_HANDLE_PRESENT;
        msg.req.info.name.size = process_name_size;
        msg.req.info.name.data = (void*)FIDL_ALLOC_PRESENT;
        memcpy(msg.process_name, process_name, process_name_size);

        msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE] = executable_vmo;
        executable_vmo = ZX_HANDLE_INVALID;

        status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB]);
        if (status != ZX_OK) {
            report_error(err_msg, "failed to duplicate job handle: %d", status);
            goto cleanup;
        }

        fuchsia_process_LauncherLaunchResponse reply;

        zx_handle_t process = ZX_HANDLE_INVALID;

        memset(&reply, 0, sizeof(reply));

        zx_channel_call_args_t args;
        args.wr_bytes = &msg;
        args.wr_handles = msg_handles;
        args.rd_bytes = &reply;
        args.rd_handles = &process;
        args.wr_num_bytes = msg_len;
        args.wr_num_handles = FDIO_SPAWN_LAUNCH_HANDLE_COUNT;
        args.rd_num_bytes = sizeof(reply);
        args.rd_num_handles = FDIO_SPAWN_LAUNCH_REPLY_HANDLE_COUNT;

        uint32_t actual_bytes = 0;
        uint32_t actual_handles = 0;

        status = zx_channel_call(launcher, 0, ZX_TIME_INFINITE, &args,
                                 &actual_bytes, &actual_handles);

        // zx_channel_call always consumes handles.
        memset(msg_handles, 0, sizeof(msg_handles));

        if (status != ZX_OK) {
            report_error(err_msg, "failed to send launch message: %d", status);
            goto cleanup;
        }

        status = reply.status;

        if (status == ZX_OK) {
            // The launcher claimed to succeed but didn't actually give us a
            // process handle. Something is wrong with the launcher.
            if (process == ZX_HANDLE_INVALID) {
                status = ZX_ERR_BAD_HANDLE;
                report_error(err_msg, "failed receive process handle");
                // This jump skips over closing the process handle, but that's
                // fine because we didn't receive a process handle.
                goto cleanup;
            }

            if (process_out) {
                *process_out = process;
                process = ZX_HANDLE_INVALID;
            }
        } else {
            report_error(err_msg, "fuchsia.process.Launcher failed");
        }

        if (process != ZX_HANDLE_INVALID)
            zx_handle_close(process);
    }

cleanup:
    if (actions) {
        for (size_t i = 0; i < action_count; ++i) {
            switch (actions[i].action) {
            case FDIO_SPAWN_ACTION_ADD_NS_ENTRY:
                zx_handle_close(actions[i].ns.handle);
                break;
            case FDIO_SPAWN_ACTION_ADD_HANDLE:
                zx_handle_close(actions[i].h.handle);
                break;
            default:
                break;
            }
        }
    }

    free(flat);

    if (executable_vmo != ZX_HANDLE_INVALID)
        zx_handle_close(executable_vmo);

    if (ldsvc != ZX_HANDLE_INVALID)
        zx_handle_close(ldsvc);

    if (launcher != ZX_HANDLE_INVALID)
        zx_handle_close(launcher);

    if (launcher_request != ZX_HANDLE_INVALID)
        zx_handle_close(launcher_request);

    if (msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE] != ZX_HANDLE_INVALID)
        zx_handle_close(msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_EXECUTABLE]);

    if (msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB] != ZX_HANDLE_INVALID)
        zx_handle_close(msg_handles[FDIO_SPAWN_LAUNCH_HANDLE_JOB]);

    // If we observe ZX_ERR_NOT_FOUND in the VMO spawn, it really means a
    // dependency of launching could not be fulfilled, but clients of spawn_etc
    // and friends could misinterpret this to mean the binary was not found.
    // Instead we remap that specific case to ZX_ERR_INTERNAL.
    if (status == ZX_ERR_NOT_FOUND) {
        return ZX_ERR_INTERNAL;
    }

    return status;
}

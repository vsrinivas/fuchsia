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

#define _GNU_SOURCE

#include <launchpad/launchpad.h>
#include "elf.h"

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>

enum special_handles {
    HND_LOADER_SVC,
    HND_EXEC_VMO,
    HND_SPECIAL_COUNT
};

struct launchpad {
    uint32_t argc;
    uint32_t envc;
    char* args;
    size_t args_len;
    char* env;
    size_t env_len;

    mx_handle_t* handles;
    uint32_t* handles_info;
    size_t handle_count;
    size_t handle_alloc;

    mx_vaddr_t entry;

    mx_handle_t special_handles[HND_SPECIAL_COUNT];
    bool loader_message;
};

// We always install the process handle as the first in the message.
#define lp_proc(lp) ((lp)->handles[0])

static void close_handles(mx_handle_t* handles, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (handles[i] != MX_HANDLE_INVALID) {
            mx_handle_close(handles[i]);
        }
    }
}

void launchpad_destroy(launchpad_t* lp) {
    close_handles(lp->special_handles, HND_SPECIAL_COUNT);
    close_handles(lp->handles, lp->handle_count);
    free(lp->handles);
    free(lp->handles_info);
    free(lp->args);
    free(lp->env);
    free(lp);
}

mx_status_t launchpad_create(const char* name, launchpad_t** result) {
    launchpad_t* lp = calloc(1, sizeof(*lp));
    if (lp == NULL)
        return ERR_NO_MEMORY;

    uint32_t name_len = MIN(strlen(name), MX_MAX_NAME_LEN);
    mx_handle_t proc = mx_process_create(name, name_len);
    if (proc < 0) {
        free(lp);
        return proc;
    }

    mx_status_t status = launchpad_add_handle(lp, proc, MX_HND_TYPE_PROC_SELF);
    if (status == NO_ERROR) {
        *result = lp;
    } else {
        mx_handle_close(proc);
        launchpad_destroy(lp);
    }

    return status;
}

mx_handle_t launchpad_get_process_handle(launchpad_t* lp) {
    return lp_proc(lp);
}

mx_status_t launchpad_arguments(launchpad_t* lp,
                                int argc, const char* const* argv) {
    if (argc < 0)
        return ERR_INVALID_ARGS;

    size_t total = 0;
    for (int i = 0; i < argc; ++i)
        total += strlen(argv[i]) + 1;

    char* buffer = NULL;
    if (total > 0) {
        buffer = malloc(total);
        if (buffer == NULL)
            return ERR_NO_MEMORY;

        char* p = buffer;
        for (int i = 0; i < argc; ++i)
            p = stpcpy(p, argv[i]) + 1;

        if ((size_t) (p - buffer) != total) {
            // The strings changed in parallel.  Not kosher!
            free(buffer);
            return ERR_INVALID_ARGS;
        }
    }

    free(lp->args);
    lp->argc = argc;
    lp->args = buffer;
    lp->args_len = total;
    return NO_ERROR;
}

mx_status_t launchpad_environ(launchpad_t* lp, const char* const* envp) {
    size_t total = 0;
    char* buffer = NULL;
    uint32_t envc = 0;

    if (envp != NULL) {
        for (const char* const* ep = envp; *ep != NULL; ++ep) {
            total += strlen(*ep) + 1;
            ++envc;
        }
    }

    if (total > 0) {
        buffer = malloc(total);
        if (buffer == NULL)
            return ERR_NO_MEMORY;

        char* p = buffer;
        for (const char* const* ep = envp; *ep != NULL; ++ep)
            p = stpcpy(p, *ep) + 1;

        if ((size_t) (p - buffer) != total) {
            // The strings changed in parallel.  Not kosher!
            free(buffer);
            return ERR_INVALID_ARGS;
        }
    }

    free(lp->env);
    lp->envc = envc;
    lp->env = buffer;
    lp->env_len = total;
    return NO_ERROR;
}

static mx_status_t more_handles(launchpad_t* lp, size_t n) {
    if (lp->handle_alloc - lp->handle_count < n) {
        size_t alloc = lp->handle_alloc == 0 ? 8 : lp->handle_alloc * 2;
        mx_handle_t* handles = realloc(lp->handles,
                                       alloc * sizeof(handles[0]));
        if (handles == NULL)
            return ERR_NO_MEMORY;
        lp->handles = handles;
        uint32_t* info = realloc(lp->handles_info, alloc * sizeof(info[0]));
        if (info == NULL)
            return ERR_NO_MEMORY;
        lp->handles_info = info;
        lp->handle_alloc = alloc;
    }
    return NO_ERROR;
}

mx_status_t launchpad_add_handle(launchpad_t* lp, mx_handle_t h, uint32_t id) {
    mx_status_t status = more_handles(lp, 1);
    if (status == NO_ERROR) {
        lp->handles[lp->handle_count] = h;
        lp->handles_info[lp->handle_count] = id;
        ++lp->handle_count;
    }
    return status;
}

mx_status_t launchpad_add_handles(launchpad_t* lp, size_t n,
                                  const mx_handle_t h[],
                                  const uint32_t id[]) {
    mx_status_t status = more_handles(lp, n);
    if (status == NO_ERROR) {
        memcpy(&lp->handles[lp->handle_count], h, n * sizeof(h[0]));
        memcpy(&lp->handles_info[lp->handle_count], id, n * sizeof(id[0]));
        lp->handle_count += n;
    }
    return status;
}

mx_status_t launchpad_elf_load_basic(launchpad_t* lp, mx_handle_t vmo) {
    if (vmo < 0)
        return vmo;
    if (vmo == MX_HANDLE_INVALID)
        return ERR_INVALID_ARGS;

    elf_load_info_t* elf;
    mx_status_t status = elf_load_start(vmo, &elf);
    if (status == NO_ERROR)
        status = elf_load_finish(lp_proc(lp), elf, vmo, &lp->entry);
    elf_load_destroy(elf);

    if (status == NO_ERROR) {
        lp->loader_message = false;
        mx_handle_close(vmo);
    }

    return status;
}

#define LOADER_SVC_MSG_MAX 1024

static mx_handle_t loader_svc_rpc(mx_handle_t loader_svc, uint32_t opcode,
                                  const void* data, size_t len) {
    struct {
        mx_loader_svc_msg_t header;
        uint8_t data[LOADER_SVC_MSG_MAX - sizeof(mx_loader_svc_msg_t)];
    } msg;

    if (len >= sizeof(msg.data))
        return ERR_NOT_ENOUGH_BUFFER;

    memset(&msg.header, 0, sizeof(msg.header));
    msg.header.opcode = opcode;
    memcpy(msg.data, data, len);
    msg.data[len] = 0;

    mx_status_t status = mx_message_write(loader_svc,
                                          &msg, sizeof(msg.header) + len + 1,
                                          NULL, 0, 0);
    if (status != NO_ERROR)
        return status;

    status = mx_handle_wait_one(loader_svc, MX_SIGNAL_READABLE,
                                MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR)
        return status;

    mx_handle_t handle = MX_HANDLE_INVALID;
    uint32_t reply_size = sizeof(msg.header);
    uint32_t handle_count = 1;
    status = mx_message_read(loader_svc, &msg, &reply_size,
                             &handle, &handle_count, 0);
    if (status != NO_ERROR)
        return status;

    // Check for protocol violations.
    if (reply_size != sizeof(msg.header)) {
    protocol_violation:
        mx_handle_close(handle);
        return ERR_BAD_STATE;
    }
    if (msg.header.opcode != LOADER_SVC_OP_STATUS)
        goto protocol_violation;

    if (msg.header.arg != NO_ERROR) {
        if (handle != MX_HANDLE_INVALID)
            goto protocol_violation;
        if (msg.header.arg > 0)
            goto protocol_violation;
        handle = msg.header.arg;
    }

    return handle;
}

static mx_status_t setup_loader_svc(launchpad_t* lp) {
    if (lp->special_handles[HND_LOADER_SVC] != MX_HANDLE_INVALID)
        return NO_ERROR;

    // TODO(mcgrathr): In the long run, this will use some long-running
    // service set up elsewhere and inherited.  For now, just spin up
    // a background thread to implement a dumb service locally.
    mx_handle_t loader_svc = mxio_loader_service();
    if (loader_svc < 0)
        return loader_svc;

    lp->special_handles[HND_LOADER_SVC] = loader_svc;
    return NO_ERROR;
}

// Consumes 'vmo' on success, not on failure.
static mx_status_t handle_interp(launchpad_t* lp, mx_handle_t vmo,
                                 const char* interp, size_t interp_len) {
    mx_status_t status = setup_loader_svc(lp);
    if (status != NO_ERROR)
        return status;

    mx_handle_t interp_vmo = loader_svc_rpc(
        lp->special_handles[HND_LOADER_SVC], LOADER_SVC_OP_LOAD_OBJECT,
        interp, interp_len);
    if (interp_vmo < 0)
        return interp_vmo;

    elf_load_info_t* elf;
    status = elf_load_start(interp_vmo, &elf);
    if (status == NO_ERROR) {
        status = elf_load_finish(lp_proc(lp), elf, interp_vmo, &lp->entry);
        elf_load_destroy(elf);
    }
    mx_handle_close(interp_vmo);

    if (status == NO_ERROR) {
        if (lp->special_handles[HND_EXEC_VMO] != MX_HANDLE_INVALID)
            mx_handle_close(lp->special_handles[HND_EXEC_VMO]);
        lp->special_handles[HND_EXEC_VMO] = vmo;
        lp->loader_message = true;
    }

    return status;
}

mx_status_t launchpad_elf_load(launchpad_t* lp, mx_handle_t vmo) {
    if (vmo < 0)
        return vmo;
    if (vmo == MX_HANDLE_INVALID)
        return ERR_INVALID_ARGS;

    elf_load_info_t* elf;
    mx_status_t status = elf_load_start(vmo, &elf);
    if (status == NO_ERROR) {
        char* interp;
        size_t interp_len;
        status = elf_load_find_interp(elf, vmo, &interp, &interp_len);
        if (status == NO_ERROR) {
            if (interp == NULL) {
                status = elf_load_finish(lp_proc(lp), elf, vmo, &lp->entry);
                if (status == NO_ERROR) {
                    lp->loader_message = false;
                    mx_handle_close(vmo);
                }
            } else {
                status = handle_interp(lp, vmo, interp, interp_len);
                free(interp);
            }
        }
        elf_load_destroy(elf);
    }
    return status;
}

mx_status_t launchpad_get_entry_address(launchpad_t* lp, mx_vaddr_t* entry) {
    if (lp->entry == 0)
        return ERR_BAD_STATE;
    *entry = lp->entry;
    return NO_ERROR;
}

bool launchpad_send_loader_message(launchpad_t* lp, bool do_send) {
    bool result = lp->loader_message;
    lp->loader_message = do_send;
    return result;
}

mx_handle_t launchpad_use_loader_service(launchpad_t* lp, mx_handle_t svc) {
    mx_handle_t result = lp->special_handles[HND_LOADER_SVC];
    lp->special_handles[HND_LOADER_SVC] = svc;
    return result;
}

static mx_status_t send_loader_message(launchpad_t* lp, mx_handle_t topipe) {
    struct loader_msg {
        mx_proc_args_t header;
        uint32_t handle_info[HND_SPECIAL_COUNT + 1];
        char env[];
    };

    const size_t msg_size = sizeof(struct loader_msg) + lp->env_len;
    struct loader_msg* msg = malloc(msg_size);
    if (msg == NULL)
        return ERR_NO_MEMORY;

    memset(&msg->header, 0, sizeof(msg->header));
    msg->header.protocol = MX_PROCARGS_PROTOCOL;
    msg->header.version = MX_PROCARGS_VERSION;
    msg->header.handle_info_off = offsetof(struct loader_msg, handle_info);

    // Include the environment strings so the dynamic linker can
    // see options like LD_DEBUG or whatnot.
    if (lp->envc > 0) {
        msg->header.environ_off = offsetof(struct loader_msg, env);
        msg->header.environ_num = lp->envc;
        memcpy(msg->env, lp->env, lp->env_len);
    }

    // This loop should be completely unrolled.  But using a switch here
    // gives us compiler warnings if we forget to handle any of the special
    // types listed in the enum.
    mx_handle_t handles[HND_SPECIAL_COUNT + 1];
    size_t nhandles = 0;
    for (enum special_handles i = 0; i <= HND_SPECIAL_COUNT; ++i) {
        uint32_t id;
        switch (i) {
        case HND_SPECIAL_COUNT:;
            // TODO(mcgrathr): The kernel doesn't permit duplicating a
            // process handle yet, so we don't actually send it.  But we
            // keep the structure of this code based on the plan to send
            // it.  So for now just send a dummy handle instead.
#if 1
            mx_handle_t proc = mx_vm_object_create(0);
#else
            // Duplicate the process handle so we can send it in the
            // loader message and still have it later.
            mx_handle_t proc = mx_handle_duplicate(lp_proc(lp),
                                                   MX_RIGHT_SAME_RIGHTS);
#endif
            if (proc < 0) {
                free(msg);
                return proc;
            }
            handles[nhandles] = proc;
            msg->handle_info[nhandles] = MX_HND_TYPE_PROC_SELF;
            ++nhandles;
            continue;

        case HND_LOADER_SVC:
            id = MX_HND_TYPE_LOADER_SVC;
            break;

        case HND_EXEC_VMO:
            id = MX_HND_TYPE_EXEC_VMO;
            break;
        }
        if (lp->special_handles[i] != MX_HANDLE_INVALID) {
            handles[nhandles] = lp->special_handles[i];
            msg->handle_info[nhandles] = id;
            ++nhandles;
        }
    }

    mx_status_t status = mx_message_write(topipe, msg, msg_size,
                                          handles, nhandles, 0);
    if (status == NO_ERROR) {
        // message_write consumed all those handles.
        for (enum special_handles i = 0; i < HND_SPECIAL_COUNT; ++i)
            lp->special_handles[i] = MX_HANDLE_INVALID;
        lp->loader_message = false;
    } else {
        // Close the process handle we duplicated.
        // The others remain live in the launchpad.
        mx_handle_close(handles[nhandles - 1]);
    }

    free(msg);
    return status;
}

// TODO(mcgrathr): One day we'll have a gather variant of message_write
// and then we can send this without copying into a temporary buffer.
static void* build_message(launchpad_t* lp, size_t *total_size) {
    size_t total = sizeof(mx_proc_args_t);
    total += lp->handle_count * sizeof(lp->handles_info[0]);
    total += lp->args_len;
    total += lp->env_len;

    uint8_t* buffer = malloc(total);
    if (buffer == NULL)
        return NULL;

    uint8_t* p = buffer;
    mx_proc_args_t* header = (void*)p;
    p += sizeof(*header);

    memset(header, 0, sizeof(*header));
    header->protocol = MX_PROCARGS_PROTOCOL;
    header->version = MX_PROCARGS_VERSION;

    header->handle_info_off = p - buffer;
    p = mempcpy(p, lp->handles_info,
                lp->handle_count * sizeof(lp->handles_info[0]));

    if (lp->argc > 0) {
        header->args_num = lp->argc;
        header->args_off = p - buffer;
        p = mempcpy(p, lp->args, lp->args_len);
    }

    if (lp->envc > 0) {
        header->environ_num = lp->envc;
        header->environ_off = p - buffer;
        p = mempcpy(p, lp->env, lp->env_len);
    }

    *total_size = total;
    return buffer;
}

mx_handle_t launchpad_start(launchpad_t* lp) {
    if (lp->entry == 0)
        return ERR_BAD_STATE;

    mx_handle_t pipeh[2];
    mx_status_t status = mx_message_pipe_create(pipeh, 0);
    if (status != NO_ERROR)
        return status;
    mx_handle_t to_child = pipeh[0];
    mx_handle_t child_bootstrap = pipeh[1];

    if (lp->loader_message) {
        status = send_loader_message(lp, to_child);
        if (status != NO_ERROR) {
            mx_handle_close(to_child);
            mx_handle_close(child_bootstrap);
            return status;
        }
    }

    size_t size;
    void* msg = build_message(lp, &size);
    if (msg == NULL) {
        mx_handle_close(to_child);
        mx_handle_close(child_bootstrap);
        return ERR_NO_MEMORY;
    }

    // TODO(mcgrathr): The kernel doesn't permit duplicating a process
    // handle yet, so we don't actually send it.  But we keep the structure
    // of this code based on the plan to send it.  So for now just send
    // a dummy handle instead.
#if 1
    mx_handle_t proc = lp_proc(lp);
    lp_proc(lp) = mx_vm_object_create(0);
#else
    // The proc handle in lp->handles[0] will be consumed by message_write.
    // So we'll need a duplicate to do process operations later.
    mx_handle_t proc = mx_handle_duplicate(lp_proc(lp), MX_RIGHT_SAME_RIGHTS);
    if (proc < 0) {
        free(msg);
        mx_handle_close(to_child);
        mx_handle_close(child_bootstrap);
        return proc;
    }
#endif

    status = mx_message_write(to_child, msg, size,
                              lp->handles, lp->handle_count, 0);
    free(msg);
    mx_handle_close(to_child);
    if (status == NO_ERROR) {
        // message_write consumed all the handles.
        for (size_t i = 0; i < lp->handle_count; ++i)
            lp->handles[i] = MX_HANDLE_INVALID;
        lp->handle_count = 0;
        status = mx_process_start(proc, child_bootstrap, lp->entry);
    }
    // process_start consumed child_bootstrap if successful.
    if (status == NO_ERROR)
        return proc;

    mx_handle_close(child_bootstrap);
    mx_handle_close(proc);

    return status;
}

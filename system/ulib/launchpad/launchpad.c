// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include "elf.h"

#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/stack.h>
#include <zircon/syscalls.h>
#include <ldmsg/ldmsg.h>
#include <fdio/io.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

enum special_handles {
    HND_LOADER_SVC,
    HND_EXEC_VMO,
    HND_SEGMENTS_VMAR,
    HND_SPECIAL_COUNT
};

struct launchpad {
    uint32_t argc;
    uint32_t envc;
    uint32_t namec;
    char* args;
    size_t args_len;
    char* env;
    size_t env_len;
    char* names;
    size_t names_len;

    size_t num_script_args;
    char* script_args;
    size_t script_args_len;

    zx_handle_t* handles;
    uint32_t* handles_info;
    size_t handle_count;
    size_t handle_alloc;

    const char* errmsg;
    zx_status_t error;

    zx_vaddr_t entry;
    zx_vaddr_t base;
    zx_vaddr_t vdso_base;

    size_t stack_size;
    bool set_stack_size;

    zx_handle_t special_handles[HND_SPECIAL_COUNT];
    bool loader_message;

    zx_handle_t reserve_vmar;
    bool fresh_process;
};

// Returned when calloc() fails on create, so callers
// can still call all the various add handles functions
// which will discard the handles, etc, etc.
static launchpad_t invalid_launchpad = {
    .errmsg = "create: could not allocate launchpad_t",
    .error = ZX_ERR_NO_MEMORY,
};

static zx_status_t lp_error(launchpad_t* lp, zx_status_t error, const char* msg) {
    if (lp->error == ZX_OK) {
        lp->error = error;
        lp->errmsg = msg;
    }
    return lp->error;
}

zx_status_t launchpad_get_status(launchpad_t* lp) {
    return lp->error;
}

void launchpad_abort(launchpad_t* lp, zx_status_t error, const char* msg) {
    lp_error(lp, (error < 0) ? error : ZX_ERR_INTERNAL, msg);
}

const char* launchpad_error_message(launchpad_t* lp) {
    return lp->errmsg;
}

#define HND_LOADER_COUNT 3
// We always install the process handle as the first in the message.
#define lp_proc(lp) ((lp)->handles[0])
// We always install the vmar handle as the second in the message.
#define lp_vmar(lp) ((lp)->handles[1])

static void close_handles(zx_handle_t* handles, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (handles[i] != ZX_HANDLE_INVALID) {
            zx_handle_close(handles[i]);
        }
    }
}

void launchpad_destroy(launchpad_t* lp) {
    if (lp == &invalid_launchpad)
        return;
    close_handles(&lp->reserve_vmar, 1);
    close_handles(lp->special_handles, HND_SPECIAL_COUNT);
    close_handles(lp->handles, lp->handle_count);
    free(lp->handles);
    free(lp->handles_info);
    free(lp->script_args);
    free(lp->args);
    free(lp->env);
    free(lp);
}

zx_status_t launchpad_create_with_process(zx_handle_t proc,
                                          zx_handle_t vmar,
                                          launchpad_t** result) {
    launchpad_t* lp = calloc(1, sizeof(*lp));
    if (lp == NULL) {
        lp = &invalid_launchpad;
    } else {
        lp->errmsg = "no error";
    }

    if (launchpad_add_handle(lp, proc, PA_PROC_SELF) == ZX_OK) {
        // If the process has an existing vDSO mapping, record it for
        // use by launchpad_start_extra.
        zx_status_t status = zx_object_get_property(
            proc, ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
            &lp->vdso_base, sizeof(lp->vdso_base));
        if (status != ZX_OK)
            lp_error(lp, status,
                     "create: cannot get ZX_PROP_PROCESS_VDSO_BASE_ADDRESS");
    }
    launchpad_add_handle(lp, vmar, PA_VMAR_ROOT);

    *result = lp;
    return lp->error;
}

// Create a new process and a launchpad that will set it up.
zx_status_t launchpad_create_with_jobs(zx_handle_t creation_job, zx_handle_t transferred_job,
                                       const char* name, launchpad_t** result) {
    uint32_t name_len = strlen(name);

    zx_handle_t proc = ZX_HANDLE_INVALID;
    zx_handle_t vmar = ZX_HANDLE_INVALID;
    zx_status_t status = zx_process_create(creation_job, name, name_len, 0, &proc, &vmar);

    launchpad_t* lp;
    if (launchpad_create_with_process(proc, vmar, &lp) == ZX_OK)
        lp->fresh_process = true;

    if (status < 0)
        lp_error(lp, status, "create: zx_process_create() failed");

    if (transferred_job != ZX_HANDLE_INVALID) {
        launchpad_add_handle(lp, transferred_job, PA_JOB_DEFAULT);
    }

    *result = lp;
    return lp->error;
}

zx_status_t launchpad_create(zx_handle_t job,
                             const char* name, launchpad_t** result) {
    if (job == ZX_HANDLE_INVALID)
        job = zx_job_default();
    zx_handle_t xjob = ZX_HANDLE_INVALID;
    zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &xjob);
    return launchpad_create_with_jobs(job, xjob, name, result);
}

zx_handle_t launchpad_get_process_handle(launchpad_t* lp) {
    return lp_proc(lp);
}

zx_handle_t launchpad_get_root_vmar_handle(launchpad_t* lp) {
    return lp_vmar(lp);
}

static zx_status_t build_stringtable(launchpad_t* lp,
                                    int count, const char* const* item,
                                    size_t* total_out, char** out) {
    if (lp->error)
        return lp->error;
    if (count < 0)
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "negative string array count");

    size_t total = 0;
    for (int i = 0; i < count; ++i)
        total += strlen(item[i]) + 1;

    char* buffer = NULL;
    if (total > 0) {
        buffer = malloc(total);
        if (buffer == NULL)
            return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for string array");

        char* p = buffer;
        for (int i = 0; i < count; ++i)
            p = stpcpy(p, item[i]) + 1;

        if ((size_t) (p - buffer) != total) {
            // The strings changed in parallel.  Not kosher!
            free(buffer);
            return lp_error(lp, ZX_ERR_INVALID_ARGS, "string array modified during use");
        }
    }

    *total_out = total;
    *out = buffer;
    return ZX_OK;
}

zx_status_t launchpad_set_args(launchpad_t* lp,
                               int argc, const char* const* argv) {
    size_t total;
    char* buffer;
    zx_status_t r = build_stringtable(lp, argc, argv, &total, &buffer);
    if (r < 0)
        return r;

    free(lp->args);
    lp->argc = argc;
    lp->args = buffer;
    lp->args_len = total;
    return ZX_OK;
}

zx_status_t launchpad_set_nametable(launchpad_t* lp,
                                    size_t count, const char* const* names) {
    size_t total;
    char* buffer;
    zx_status_t r = build_stringtable(lp, count, names, &total, &buffer);
    if (r < 0)
        return r;

    free(lp->names);
    lp->namec = count;
    lp->names = buffer;
    lp->names_len = total;
    return ZX_OK;
}

zx_status_t launchpad_set_environ(launchpad_t* lp, const char* const* envp) {
    uint32_t count = 0;
    if (envp != NULL) {
        for (const char* const* ep = envp; *ep != NULL; ++ep) {
            ++count;
        }
    }

    size_t total;
    char* buffer;
    zx_status_t r = build_stringtable(lp, count, envp, &total, &buffer);
    if (r < 0)
        return r;

    free(lp->env);
    lp->envc = count;
    lp->env = buffer;
    lp->env_len = total;
    return ZX_OK;
}

static zx_status_t more_handles(launchpad_t* lp, size_t n) {
    if (lp->error)
        return lp->error;

    if (ZX_CHANNEL_MAX_MSG_HANDLES - lp->handle_count < n)
        return lp_error(lp, ZX_ERR_NO_MEMORY, "too many handles for handle table");

    if (lp->handle_alloc - lp->handle_count < n) {
        size_t alloc = lp->handle_alloc == 0 ? 8 : lp->handle_alloc * 2;
        while (alloc - lp->handle_count < n)
            alloc <<= 1;
        zx_handle_t* handles = realloc(lp->handles,
                                       alloc * sizeof(handles[0]));
        if (handles == NULL)
            return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for handle table");
        lp->handles = handles;
        uint32_t* info = realloc(lp->handles_info, alloc * sizeof(info[0]));
        if (info == NULL)
            return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for handle table");
        lp->handles_info = info;
        lp->handle_alloc = alloc;
    }
    return ZX_OK;
}

zx_status_t launchpad_add_handle(launchpad_t* lp, zx_handle_t h, uint32_t id) {
    if (h == ZX_HANDLE_INVALID)
        return lp_error(lp, ZX_ERR_BAD_HANDLE, "added invalid handle");
    zx_status_t status = more_handles(lp, 1);
    if (status == ZX_OK) {
        lp->handles[lp->handle_count] = h;
        lp->handles_info[lp->handle_count] = id;
        ++lp->handle_count;
    } else {
        zx_handle_close(h);
    }
    return status;
}

zx_status_t launchpad_add_handles(launchpad_t* lp, size_t n,
                                  const zx_handle_t h[],
                                  const uint32_t id[]) {
    zx_status_t status = more_handles(lp, n);
    if (status == ZX_OK) {
        memcpy(&lp->handles[lp->handle_count], h, n * sizeof(h[0]));
        memcpy(&lp->handles_info[lp->handle_count], id, n * sizeof(id[0]));
        lp->handle_count += n;
        for (size_t i = 0; i < n; i++) {
            if (h[i] == ZX_HANDLE_INVALID) {
                return lp_error(lp, ZX_ERR_BAD_HANDLE, "added invalid handle");
            }
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            zx_handle_close(h[i]);
        }
    }
    return status;
}

//TODO: use transfer_fd here and eliminate fdio_pipe_half()
zx_status_t launchpad_add_pipe(launchpad_t* lp, int* fd_out, int target_fd) {
    zx_handle_t handle;
    uint32_t id;
    int fd;

    if (lp->error)
        return lp->error;
    if ((target_fd < 0) || (target_fd >= FDIO_MAX_FD)) {
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "add_pipe: invalid target fd");
    }
    zx_status_t status;
    if ((status = fdio_pipe_half(&handle, &id)) < 0) {
        return lp_error(lp, status, "add_pipe: failed to create pipe");
    }
    fd = status;
    if ((status = launchpad_add_handle(lp, handle, PA_HND(PA_HND_TYPE(id), target_fd))) < 0) {
        close(fd);
        zx_handle_close(handle);
        return status;
    }
    *fd_out = fd;
    return ZX_OK;
}

static void check_elf_stack_size(launchpad_t* lp, elf_load_info_t* elf) {
    size_t elf_stack_size = elf_load_get_stack_size(elf);
    if (elf_stack_size > 0)
        launchpad_set_stack_size(lp, elf_stack_size);
}

zx_status_t launchpad_elf_load_basic(launchpad_t* lp, zx_handle_t vmo) {
    if (vmo == ZX_HANDLE_INVALID)
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load: invalid vmo");
    if (lp->error)
        goto done;

    elf_load_info_t* elf;
    zx_status_t status;
    if ((status = elf_load_start(vmo, NULL, 0, &elf)))
        lp_error(lp, status, "elf_load: elf_load_start() failed");
    zx_handle_t segments_vmar;
    if ((status = elf_load_finish(lp_vmar(lp), elf, vmo,
                                  &segments_vmar, &lp->base, &lp->entry)))
        lp_error(lp, status, "elf_load: elf_load_finish() failed");
    check_elf_stack_size(lp, elf);
    elf_load_destroy(elf);

    if (status == ZX_OK) {
        lp->loader_message = false;
        launchpad_add_handle(lp, segments_vmar,
                             PA_HND(PA_VMAR_LOADED, 0));
    }

done:
    zx_handle_close(vmo);
    return lp->error;
}

zx_status_t launchpad_elf_load_extra(launchpad_t* lp, zx_handle_t vmo,
                                     zx_vaddr_t* base, zx_vaddr_t* entry) {
    if (lp->error)
        return lp->error;
    if (vmo == ZX_HANDLE_INVALID)
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load_extra: invalid vmo");

    elf_load_info_t* elf;
    zx_status_t status;
    if ((status = elf_load_start(vmo, NULL, 0, &elf)))
        lp_error(lp, status, "elf_load_extra: elf_load_start() failed");
    if ((status = elf_load_finish(lp_vmar(lp), elf, vmo, NULL, base, entry)))
        lp_error(lp, status, "elf_load_extra: elf_load_finish() failed");
    elf_load_destroy(elf);

    return lp->error;
}

#define LOADER_SVC_MSG_MAX 1024

static zx_status_t loader_svc_rpc(zx_handle_t loader_svc, uint32_t ordinal,
                                  const void* data, size_t len, zx_handle_t* out) {
    static _Atomic zx_txid_t next_txid;

    ldmsg_req_t req;
    memset(&req.header, 0, sizeof(req.header));
    req.header.ordinal = ordinal;

    size_t req_len;
    zx_status_t status = ldmsg_req_encode(&req, &req_len, data, len);
    if (status != ZX_OK)
        return status;

    req.header.txid = atomic_fetch_add(&next_txid, 1);

    ldmsg_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    zx_handle_t handle = ZX_HANDLE_INVALID;
    const zx_channel_call_args_t call = {
        .wr_bytes = &req,
        .wr_num_bytes = req_len,
        .rd_bytes = &rsp,
        .rd_num_bytes = sizeof(rsp),
        .rd_handles = &handle,
        .rd_num_handles = 1,
    };
    uint32_t reply_size;
    uint32_t handle_count;
    zx_status_t read_status = ZX_OK;
    status = zx_channel_call(loader_svc, 0, ZX_TIME_INFINITE, &call, &reply_size,
                             &handle_count, &read_status);
    if (status != ZX_OK) {
        return status == ZX_ERR_CALL_FAILED ? read_status : status;
    }

    // Check for protocol violations.
    if (reply_size != ldmsg_rsp_get_size(&rsp)) {
    protocol_violation:
        zx_handle_close(handle);
        return ZX_ERR_BAD_STATE;
    }
    if (rsp.header.ordinal != ordinal)
        goto protocol_violation;

    if (rsp.rv != ZX_OK) {
        if (handle != ZX_HANDLE_INVALID)
            goto protocol_violation;
        if (rsp.rv > 0)
            goto protocol_violation;
        *out = ZX_HANDLE_INVALID;
    } else {
        *out = handle_count ? handle : ZX_HANDLE_INVALID;
    }
    return rsp.rv;
}

static zx_status_t setup_loader_svc(launchpad_t* lp) {
    if (lp->special_handles[HND_LOADER_SVC] != ZX_HANDLE_INVALID)
        return ZX_OK;

    zx_handle_t loader_svc;
    zx_status_t status = dl_clone_loader_service(&loader_svc);
    if (status < 0)
        return status;

    lp->special_handles[HND_LOADER_SVC] = loader_svc;
    return ZX_OK;
}

// Reserve roughly the low half of the address space, so the new
// process can use sanitizers that need to allocate shadow memory there.
// The reservation VMAR is kept around just long enough to make sure all
// the initial allocations (mapping in the initial ELF objects, and
// allocating the initial stack) stay out of this area, and then destroyed.
// The process's own allocations can then use the full address space; if
// it's using a sanitizer, it will set up its shadow memory first thing.
static zx_status_t reserve_low_address_space(launchpad_t* lp) {
    if (lp->reserve_vmar != ZX_HANDLE_INVALID)
        return ZX_OK;

    zx_info_vmar_t info;
    zx_status_t status = zx_object_get_info(lp_vmar(lp), ZX_INFO_VMAR,
                                            &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        return lp_error(lp, status,
                        "zx_object_get_info failed on child root VMAR handle");
    }

    uintptr_t addr;
    size_t reserve_size =
        (((info.base + info.len) / 2) + PAGE_SIZE - 1) & -PAGE_SIZE;
    status = zx_vmar_allocate(lp_vmar(lp), 0, reserve_size - info.base,
                              ZX_VM_FLAG_SPECIFIC, &lp->reserve_vmar, &addr);
    if (status != ZX_OK) {
        return lp_error(
            lp, status,
            "zx_vmar_allocate failed for low address space reservation");
    }

    if (addr != info.base) {
        return lp_error(lp, ZX_ERR_BAD_STATE,
                        "zx_vmar_allocate gave wrong address?!?");
    }

    return ZX_OK;
}

// Consumes 'vmo' on success, not on failure.
static zx_status_t handle_interp(launchpad_t* lp, zx_handle_t vmo,
                                 const char* interp, size_t interp_len) {
    zx_status_t status = setup_loader_svc(lp);
    if (status != ZX_OK)
        return status;

    zx_handle_t interp_vmo;
    status = loader_svc_rpc(
        lp->special_handles[HND_LOADER_SVC], LDMSG_OP_LOAD_OBJECT,
        interp, interp_len, &interp_vmo);
    if (status != ZX_OK)
        return status;

    if (lp->fresh_process) {
        // A fresh process using PT_INTERP might be loading a libc.so that
        // supports sanitizers, so in that case (the most common case)
        // keep the mappings launchpad makes out of the low address region.
        status = reserve_low_address_space(lp);
        if (status != ZX_OK)
            return status;
    }

    elf_load_info_t* elf;
    zx_handle_t segments_vmar;
    status = elf_load_start(interp_vmo, NULL, 0, &elf);
    if (status == ZX_OK) {
        status = elf_load_finish(lp_vmar(lp), elf, interp_vmo,
                                 &segments_vmar, &lp->base, &lp->entry);
        elf_load_destroy(elf);
    }
    zx_handle_close(interp_vmo);

    if (status == ZX_OK) {
        if (lp->special_handles[HND_EXEC_VMO] != ZX_HANDLE_INVALID)
            zx_handle_close(lp->special_handles[HND_EXEC_VMO]);
        lp->special_handles[HND_EXEC_VMO] = vmo;
        if (lp->special_handles[HND_SEGMENTS_VMAR] != ZX_HANDLE_INVALID)
            zx_handle_close(lp->special_handles[HND_SEGMENTS_VMAR]);
        lp->special_handles[HND_SEGMENTS_VMAR] = segments_vmar;
        lp->loader_message = true;
    }

    return status;
}

static zx_status_t launchpad_elf_load_body(launchpad_t* lp, const char* hdr_buf,
                                           size_t buf_sz, zx_handle_t vmo) {
    elf_load_info_t* elf;
    zx_status_t status;

    if (lp->error)
        goto done;
    if ((status = elf_load_start(vmo, hdr_buf, buf_sz, &elf)) != ZX_OK) {
        lp_error(lp, status, "elf_load: elf_load_start() failed");
    } else {
        char* interp;
        size_t interp_len;
        status = elf_load_get_interp(elf, vmo, &interp, &interp_len);
        if (status != ZX_OK) {
            lp_error(lp, status, "elf_load: get_interp() failed");
        } else {
            if (interp == NULL) {
                zx_handle_t segments_vmar;
                status = elf_load_finish(lp_vmar(lp), elf, vmo, &segments_vmar,
                                         &lp->base, &lp->entry);
                if (status != ZX_OK) {
                    lp_error(lp, status, "elf_load: elf_load_finish() failed");
                } else {
                    // With no PT_INTERP, we obey PT_GNU_STACK.p_memsz for
                    // the stack size setting.  With PT_INTERP, the dynamic
                    // linker is responsible for that.
                    check_elf_stack_size(lp, elf);
                    lp->loader_message = false;
                    launchpad_add_handle(
                        lp, segments_vmar,
                        PA_HND(PA_VMAR_LOADED, 0));
                }
            } else {
                if ((status = handle_interp(lp, vmo, interp, interp_len))) {
                    lp_error(lp, status, "elf_load: handle_interp failed");
                } else {
                    // handle_interp() takes ownership of vmo on success
                    vmo = ZX_HANDLE_INVALID;
                }
                free(interp);
            }
        }
        elf_load_destroy(elf);
    }
done:
    if (vmo)
        zx_handle_close(vmo);
    return lp->error;
}

// Find the starting point of the interpreter and the interpreter
// arguments in a #! script header. Note that the input buffer (line)
// will be modified to add a NULL after the interpreter name.
static zx_status_t parse_interp_spec(char *line, char **interp_start,
                                     size_t *interp_len, char **args_start)
{
    *args_start = NULL;

    // Skip the '#!' prefix
    char* next_char = line + 2;

    // Skip whitespace
    next_char += strspn(next_char, " \t");

    // No interpreter specified
    if (*next_char == '\0')
        return ZX_ERR_NOT_FOUND;

    *interp_start = next_char;

    // Skip the interpreter name
    next_char += strcspn(next_char, " \t");
    *interp_len = next_char - *interp_start;

    if (*next_char == '\0')
        return ZX_OK;

    *next_char++ = '\0';

    // Look for the args
    next_char += strspn(next_char, " \t");

    if (*next_char == '\0')
        return ZX_OK;

    *args_start = next_char;
    return ZX_OK;
}

zx_status_t launchpad_file_load(launchpad_t* lp, zx_handle_t vmo) {
    if (vmo == ZX_HANDLE_INVALID)
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "file_load: invalid vmo");

    if (lp->script_args != NULL) {
        free(lp->script_args);
        lp->script_args = NULL;
    }
    lp->script_args_len = 0;
    lp->num_script_args = 0;

    size_t script_nest_level = 0;

    char first_line[LP_MAX_INTERP_LINE_LEN + 1];
    size_t to_read = sizeof(first_line);
    size_t vmo_size;
    zx_status_t status = zx_vmo_get_size(vmo, &vmo_size);
    if (status != ZX_OK) {
        return lp_error(lp, status, "file_load: zx_vmo_get_size() failed");
    }
    if (to_read > vmo_size) {
        to_read = vmo_size;
    }

    while (1) {
        // Read enough to get the interpreter specification of a script
        status = zx_vmo_read(vmo, first_line, 0, to_read);

        // This is not a script -- load as an ELF file
        if ((status == ZX_OK)
            && (to_read < 2 || first_line[0] != '#' || first_line[1] != '!'))
            break;

        zx_handle_close(vmo);

        if (status != ZX_OK)
            return lp_error(lp, status, "file_load: zx_vmo_read() failed");

        script_nest_level++;

        // No point trying to read an interpreter we're not going to consider
        if (script_nest_level > LP_MAX_SCRIPT_NEST_LEVEL)
            return lp_error(lp, ZX_ERR_NOT_SUPPORTED,
                            "file_load: too many levels of script indirection");

        // Normalize the line so that it is NULL-terminated
        char* newline_pos = memchr(first_line, '\n', to_read);
        if (newline_pos)
            *newline_pos = '\0';
        else if (to_read == sizeof(first_line))
            return lp_error(lp, ZX_ERR_OUT_OF_RANGE,
                            "file_load: first line of script too long");
        else
            first_line[to_read] = '\0';

        char* interp_start;
        size_t interp_len;
        char* args_start;
        status = parse_interp_spec(first_line, &interp_start, &interp_len,
                                   &args_start);
        if (status != ZX_OK)
            return lp_error(lp, status,
                            "file_load: failed to parse interpreter spec");

        size_t args_len = (args_start == NULL) ? 0 : newline_pos - args_start;

        // Add interpreter and args to start of lp->script_args
        size_t new_args_len = interp_len + 1;
        if (args_start != NULL)
            new_args_len += args_len + 1;
        char *new_buf = malloc(new_args_len + lp->script_args_len);
        if (new_buf == NULL)
            return lp_error(lp, ZX_ERR_NO_MEMORY, "file_load: out of memory");

        memcpy(new_buf, interp_start, interp_len + 1);
        lp->num_script_args++;

        if (args_start != NULL) {
            memcpy(&new_buf[interp_len + 1], args_start, args_len + 1);
            lp->num_script_args++;
        }

        if (lp->script_args != NULL) {
            memcpy(&new_buf[new_args_len], lp->script_args,
                   lp->script_args_len);
            free(lp->script_args);
        }

        lp->script_args = new_buf;
        lp->script_args_len += new_args_len;

        // Load the interpreter into memory
        status = setup_loader_svc(lp);
        if (status != ZX_OK)
            return lp_error(lp, status, "file_load: setup_loader_svc() failed");

        status = loader_svc_rpc(lp->special_handles[HND_LOADER_SVC],
                             LDMSG_OP_LOAD_SCRIPT_INTERPRETER,
                             interp_start, interp_len, &vmo);
        if (status != ZX_OK)
            return lp_error(lp, status, "file_load: loader_svc_rpc() failed");
    }

    // Finally, load the interpreter itself
    status = launchpad_elf_load_body(lp, first_line, to_read, vmo);

    if (status != ZX_OK)
        lp_error(lp, status, "file_load: failed to load ELF file");

    return status;
}

zx_status_t launchpad_elf_load(launchpad_t* lp, zx_handle_t vmo) {
    if (vmo == ZX_HANDLE_INVALID)
        return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load: invalid vmo");

    return launchpad_elf_load_body(lp, NULL, 0, vmo);
}

static zx_handle_t vdso_vmo = ZX_HANDLE_INVALID;
static mtx_t vdso_mutex = MTX_INIT;
static void vdso_lock(void) __TA_ACQUIRE(&vdso_mutex) {
    mtx_lock(&vdso_mutex);
}
static void vdso_unlock(void) __TA_RELEASE(&vdso_mutex) {
    mtx_unlock(&vdso_mutex);
}
static zx_handle_t vdso_get_vmo(void) {
    if (vdso_vmo == ZX_HANDLE_INVALID)
        vdso_vmo = zx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0));
    return vdso_vmo;
}

zx_status_t launchpad_get_vdso_vmo(zx_handle_t* out) {
    vdso_lock();
    zx_status_t status = zx_handle_duplicate(vdso_get_vmo(),
                                             ZX_RIGHT_SAME_RIGHTS, out);
    vdso_unlock();
    return status;
}

zx_handle_t launchpad_set_vdso_vmo(zx_handle_t new_vdso_vmo) {
    vdso_lock();
    zx_handle_t old = vdso_vmo;
    vdso_vmo = new_vdso_vmo;
    vdso_unlock();
    return old;
}

zx_status_t launchpad_add_vdso_vmo(launchpad_t* lp) {
    if (lp->error)
        return lp->error;
    zx_handle_t vdso;
    zx_status_t status;
    if ((status = launchpad_get_vdso_vmo(&vdso)) != ZX_OK)
        return lp_error(lp, status, "add_vdso_vmo: get_vdso_vmo failed");
    // Takes ownership of 'vdso'.
    return launchpad_add_handle(lp, vdso, PA_HND(PA_VMO_VDSO, 0));
}

zx_status_t launchpad_load_vdso(launchpad_t* lp, zx_handle_t vmo) {
    if (vmo != ZX_HANDLE_INVALID)
        return launchpad_elf_load_extra(lp, vmo, &lp->vdso_base, NULL);
    vdso_lock();
    vmo = vdso_get_vmo();
    zx_status_t status = launchpad_elf_load_extra(lp, vmo,
                                                  &lp->vdso_base, NULL);
    vdso_unlock();
    return status;
}

zx_status_t launchpad_get_entry_address(launchpad_t* lp, zx_vaddr_t* entry) {
    if (lp->entry == 0)
        return ZX_ERR_BAD_STATE;
    *entry = lp->entry;
    return ZX_OK;
}

zx_status_t launchpad_get_base_address(launchpad_t* lp, zx_vaddr_t* base) {
    if (lp->base == 0)
        return ZX_ERR_BAD_STATE;
    *base = lp->base;
    return ZX_OK;
}

bool launchpad_send_loader_message(launchpad_t* lp, bool do_send) {
    bool result = lp->loader_message;
    if (!lp->error)
        lp->loader_message = do_send;
    return result;
}

zx_handle_t launchpad_use_loader_service(launchpad_t* lp, zx_handle_t svc) {
    if (lp->error) {
        zx_handle_close(svc);
        return lp->error;
    }
    zx_handle_t result = lp->special_handles[HND_LOADER_SVC];
    lp->special_handles[HND_LOADER_SVC] = svc;
    return result;
}

// Construct a load message. Fill in the header, args, and environment
// fields, and leave space for the handles, which should be filled in
// by the caller.
// TODO(mcgrathr): One day we'll have a gather variant of message_write
// and then we can send this without copying into a temporary buffer.
static zx_status_t build_message(launchpad_t* lp, size_t num_handles,
                                 void** msg_buf, size_t* buf_size,
                                 bool with_names) {

    size_t msg_size = sizeof(zx_proc_args_t);
    static_assert(sizeof(zx_proc_args_t) % sizeof(uint32_t) == 0,
                  "handles misaligned in load message");
    msg_size += sizeof(uint32_t) * num_handles;
    msg_size += lp->script_args_len;
    msg_size += lp->args_len;
    msg_size += lp->env_len;
    msg_size += lp->names_len;
    void* msg = malloc(msg_size);
    if (msg == NULL)
        return ZX_ERR_NO_MEMORY;

    zx_proc_args_t* header = msg;

    memset(header, 0, sizeof(*header));
    header->protocol = ZX_PROCARGS_PROTOCOL;
    header->version = ZX_PROCARGS_VERSION;
    header->handle_info_off = sizeof(*header);

    // Include the argument strings so the dynamic linker can use argv[0]
    // in messages it prints.
    header->args_off = header->handle_info_off +
                       sizeof(uint32_t) * num_handles;
    header->args_num = lp->num_script_args + lp->argc;
    if (header->args_num > 0) {
        uint8_t* script_args_start = (uint8_t*)msg + header->args_off;
        memcpy(script_args_start, lp->script_args, lp->script_args_len);
        uint8_t* args_start = script_args_start + lp->script_args_len;
        memcpy(args_start, lp->args, lp->args_len);
    }
    size_t total_args_len = lp->script_args_len + lp->args_len;

    // Include the environment strings so the dynamic linker can
    // see options like LD_DEBUG or whatnot.
    if (lp->envc > 0) {
        header->environ_off = header->args_off + total_args_len;
        header->environ_num = lp->envc;
        uint8_t* env_start = (uint8_t*)msg + header->environ_off;
        memcpy(env_start, lp->env, lp->env_len);
    }

    if (with_names && (lp->namec > 0)) {
        header->names_off = header->args_off + total_args_len + lp->env_len;
        header->names_num = lp->namec;
        uint8_t* names_start = (uint8_t*)msg + header->names_off;
        memcpy(names_start, lp->names, lp->names_len);
    }

    *msg_buf = msg;
    *buf_size = msg_size;
    return ZX_OK;
}

static zx_status_t send_loader_message(launchpad_t* lp,
                                       zx_handle_t first_thread,
                                       zx_handle_t tochannel) {
    void* msg;
    size_t msg_size;
    size_t num_handles = HND_SPECIAL_COUNT + HND_LOADER_COUNT;

    zx_status_t status = build_message(lp, num_handles, &msg, &msg_size, false);
    if (status != ZX_OK)
        return status;

    zx_proc_args_t* header = msg;
    uint32_t* msg_handle_info;
    msg_handle_info = (uint32_t*) ((uint8_t*)msg + header->handle_info_off);

    // This loop should be completely unrolled.  But using a switch here
    // gives us compiler warnings if we forget to handle any of the special
    // types listed in the enum.
    zx_handle_t handles[HND_SPECIAL_COUNT + HND_LOADER_COUNT];
    size_t nhandles = 0;
    for (enum special_handles i = 0; i <= HND_SPECIAL_COUNT; ++i) {
        uint32_t id = 0; // -Wall
        switch (i) {
        case HND_SPECIAL_COUNT:;
            // Duplicate the handles for the loader so we can send them in the
            // loader message and still have them later.
            zx_handle_t proc;
            status = zx_handle_duplicate(lp_proc(lp), ZX_RIGHT_SAME_RIGHTS, &proc);
            if (status != ZX_OK) {
                free(msg);
                return status;
            }
            zx_handle_t vmar;
            status = zx_handle_duplicate(lp_vmar(lp), ZX_RIGHT_SAME_RIGHTS, &vmar);
            if (status != ZX_OK) {
                zx_handle_close(proc);
                free(msg);
                return status;
            }
            zx_handle_t thread;
            status = zx_handle_duplicate(first_thread, ZX_RIGHT_SAME_RIGHTS, &thread);
            if (status != ZX_OK) {
                zx_handle_close(proc);
                zx_handle_close(vmar);
                free(msg);
                return status;
            }
            handles[nhandles] = proc;
            msg_handle_info[nhandles] = PA_PROC_SELF;
            handles[nhandles + 1] = vmar;
            msg_handle_info[nhandles + 1] = PA_VMAR_ROOT;
            handles[nhandles + 2] = thread;
            msg_handle_info[nhandles + 2] = PA_THREAD_SELF;
            nhandles += HND_LOADER_COUNT;
            continue;

        case HND_LOADER_SVC:
            id = PA_SVC_LOADER;
            break;

        case HND_EXEC_VMO:
            id = PA_VMO_EXECUTABLE;
            break;

        case HND_SEGMENTS_VMAR:
            id = PA_VMAR_LOADED;
            break;
        }
        if (lp->special_handles[i] != ZX_HANDLE_INVALID) {
            handles[nhandles] = lp->special_handles[i];
            msg_handle_info[nhandles] = id;
            ++nhandles;
        }
    }

    status = zx_channel_write(tochannel, 0, msg, msg_size, handles, nhandles);
    if (status == ZX_OK) {
        // message_write consumed all those handles.
        for (enum special_handles i = 0; i < HND_SPECIAL_COUNT; ++i)
            lp->special_handles[i] = ZX_HANDLE_INVALID;
        lp->loader_message = false;
    } else {
        // Close the handles we duplicated for the loader.
        // The others remain live in the launchpad.
        for (int i = 1; i <= HND_LOADER_COUNT; i++) {
          zx_handle_close(handles[nhandles - i]);
        }
    }

    free(msg);
    return status;
}

size_t launchpad_set_stack_size(launchpad_t* lp, size_t new_size) {
    size_t old_size = lp->stack_size;
    if (new_size >= (SIZE_MAX & -PAGE_SIZE)) {
        // Ridiculously large size won't actually work at allocation time,
        // but at least page rounding won't wrap it around to zero.
        new_size = SIZE_MAX & -PAGE_SIZE;
    } else if (new_size > 0) {
        // Round up to page size.
        new_size = (new_size + PAGE_SIZE - 1) & -PAGE_SIZE;
    }
    if (lp->error == ZX_OK) {
        lp->stack_size = new_size;
        lp->set_stack_size = true;
    }
    return old_size;
}

static zx_status_t prepare_start(launchpad_t* lp, const char* thread_name,
                                 zx_handle_t to_child,
                                 zx_handle_t* thread, uintptr_t* sp) {
    if (lp->entry == 0)
        return lp_error(lp, ZX_ERR_BAD_STATE, "prepare start bad state");

    zx_status_t status = zx_thread_create(lp_proc(lp), thread_name,
                                          strlen(thread_name), 0, thread);
    if (status < 0) {
        return lp_error(lp, status, "cannot create initial thread");
    } else {
        // Pass the thread handle down to the child.  The handle we pass
        // will be consumed by message_write.  So we need a duplicate to
        // pass to zx_process_start later.
        zx_handle_t thread_copy;
        status = zx_handle_duplicate(*thread, ZX_RIGHT_SAME_RIGHTS, &thread_copy);
        if (status < 0) {
            zx_handle_close(*thread);
            return lp_error(lp, status, "cannot duplicate thread handle");
        }
        status = launchpad_add_handle(lp, thread_copy, PA_THREAD_SELF);
        if (status != ZX_OK) {
            zx_handle_close(*thread);
            return lp_error(lp, status, "cannot add thread self handle");
        }
    }

    bool sent_loader_message = lp->loader_message;
    if (lp->loader_message) {
        status = send_loader_message(lp, *thread, to_child);
        if (status != ZX_OK) {
            zx_handle_close(*thread);
            return lp_error(lp, status, "failed to send loader message");
        }
    }

    bool allocate_stack = !lp->set_stack_size || lp->stack_size > 0;

    void *msg = NULL;
    size_t size;

    if (build_message(lp, lp->handle_count + (allocate_stack ? 1 : 0),
                      &msg, &size, true) != ZX_OK) {
        zx_handle_close(*thread);
        return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory assembling procargs message");
    }
    zx_proc_args_t* header = msg;
    uint32_t* next_handle = mempcpy((uint8_t*)msg + header->handle_info_off,
                                    lp->handles_info,
                                    lp->handle_count * sizeof(lp->handles_info[0]));
    if (allocate_stack)
        *next_handle = PA_VMO_STACK;

    // Figure out how big an initial thread to allocate.
    char stack_vmo_name[ZX_MAX_NAME_LEN];
    size_t stack_size;
    if (sent_loader_message && !lp->set_stack_size) {
        // The initial stack will be used just for startup work and to
        // contain the bootstrap messages.  Make it only as big as needed.
        // This constant is defined by the C library in <limits.h>.  It's
        // tuned to be enough to cover the dynamic linker and C library
        // startup code's stack usage (up until the point it switches to
        // its own stack in __libc_start_main), but leave a little space so
        // for small bootstrap message sizes the stack needs only one page.
        stack_size = size + PTHREAD_STACK_MIN;
        stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;
        snprintf(stack_vmo_name, sizeof(stack_vmo_name),
                 "stack: msg of %#zx", size);
    } else {
        // Use the requested or default size.
        stack_size =
            lp->set_stack_size ? lp->stack_size : ZIRCON_DEFAULT_STACK_SIZE;
        snprintf(stack_vmo_name, sizeof(stack_vmo_name), "stack: %s %#zx",
                 lp->set_stack_size ? "explicit" : "default", stack_size);

        // Assume the process will read the bootstrap message onto its
        // initial thread's stack.  If it would need more than half its
        // stack just to read the message, consider that an unreasonably
        // large size for the message (presumably arguments and
        // environment strings that are unreasonably large).
        if (stack_size > 0 && size > stack_size / 2) {
            free(msg);
            zx_handle_close(*thread);
            return lp_error(lp, ZX_ERR_BUFFER_TOO_SMALL,
                            "procargs message is too large");
        }
    }

    *sp = 0;
    if (stack_size > 0) {
        // Allocate the initial thread's stack.
        zx_handle_t stack_vmo;
        zx_status_t status = zx_vmo_create(stack_size, 0, &stack_vmo);
        if (status != ZX_OK) {
            free(msg);
            zx_handle_close(*thread);
            return lp_error(lp, status, "cannot create stack vmo");
        }
        zx_object_set_property(stack_vmo, ZX_PROP_NAME,
                               stack_vmo_name, strlen(stack_vmo_name));
        zx_vaddr_t stack_base;
        status = zx_vmar_map(lp_vmar(lp), 0, stack_vmo, 0, stack_size,
                              ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                              &stack_base);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(stack_size % PAGE_SIZE == 0);
            *sp = compute_initial_stack_pointer(stack_base, stack_size);
            // Pass the stack VMO to the process.  Our protocol with the
            // new process is that we warrant that this is the VMO from
            // which the initial stack is mapped and that we've exactly
            // mapped the entire thing, so vm_object_get_size on this in
            // concert with the initial SP value tells it the exact bounds
            // of its stack.
            //
            // Note this expands the handle list after we've already
            // built the bootstrap message.  We shoved an extra info
            // slot with PA_VMO_STACK into the message, so now this new
            // final handle will correspond to that slot.
            status = launchpad_add_handle(lp, stack_vmo, PA_VMO_STACK);
        }
        if (status != ZX_OK) {
            zx_handle_close(stack_vmo);
            zx_handle_close(*thread);
            free(msg);
            return lp_error(lp, status, "cannot map stack vmo");
        }
    }

    if (lp->reserve_vmar != ZX_HANDLE_INVALID) {
        // We're done doing mappings, so clear out the reservation VMAR.
        status = zx_vmar_destroy(lp->reserve_vmar);
        if (status != ZX_OK) {
            return lp_error(lp, status, "\
zx_vmar_destroy failed on low address space reservation VMAR");
        }
        status = zx_handle_close(lp->reserve_vmar);
        if (status != ZX_OK) {
            return lp_error(lp, status, "\
zx_handle_close failed on low address space reservation VMAR");
        }
        lp->reserve_vmar = ZX_HANDLE_INVALID;
    }

    status = zx_channel_write(to_child, 0, msg, size,
                              lp->handles, lp->handle_count);
    free(msg);
    if (status == ZX_OK) {
        // message_write consumed all the handles.
        for (size_t i = 0; i < lp->handle_count; ++i)
            lp->handles[i] = ZX_HANDLE_INVALID;
        lp->handle_count = 0;
    } else {
        zx_handle_close(*thread);
        return lp_error(lp, status, "failed to write procargs message");
    }

    return ZX_OK;
}

// Start the process running.  If the send_loader_message flag is
// set and this succeeds in sending the initial bootstrap message,
// it clears the loader-service handle.  If this succeeds in sending
// the main bootstrap message, it clears the list of handles to
// transfer (after they've been transferred) as well as the process
// handle.
//
// Returns the process handle via |process_out| on success, giving
// ownership to the caller.  On failure, the return value doesn't
// distinguish failure to send the first or second message from
// failure to start the process, so on failure the loader-service
// handle might or might not have been cleared and the handles to
// transfer might or might not have been cleared.
static zx_status_t launchpad_start(launchpad_t* lp, zx_handle_t* process_out) {
    if (lp->error)
        return lp->error;

    // The proc handle in lp->handles[0] will be consumed by message_write.
    // So we'll need a duplicate to do process operations later.
    zx_handle_t proc;
    zx_status_t status = zx_handle_duplicate(lp_proc(lp), ZX_RIGHT_SAME_RIGHTS, &proc);
    if (status < 0)
        return lp_error(lp, status, "start: cannot duplicate process handle");

    zx_handle_t channelh[2];
    status = zx_channel_create(0, channelh, channelh + 1);
    if (status != ZX_OK) {
        zx_handle_close(proc);
        return lp_error(lp, status, "start: cannot create channel");
    }
    zx_handle_t to_child = channelh[0];
    zx_handle_t child_bootstrap = channelh[1];

    zx_handle_t thread;
    uintptr_t sp;
    status = prepare_start(lp, "initial-thread", to_child, &thread, &sp);
    zx_handle_close(to_child);
    if (status != ZX_OK) {
        lp_error(lp, status, "start: prepare_start() failed");
    } else {
        status = zx_process_start(proc, thread, lp->entry, sp,
                                  child_bootstrap, lp->vdso_base);
        if (status != ZX_OK)
            lp_error(lp, status, "start: zx_process_start() failed");
        zx_handle_close(thread);
    }
    // process_start consumed child_bootstrap if successful.
    if (status == ZX_OK) {
        *process_out = proc;
        return ZX_OK;
    }

    zx_handle_close(proc);
    zx_handle_close(child_bootstrap);
    return status;
}

zx_status_t launchpad_start_injected(launchpad_t* lp, const char* thread_name,
                                     zx_handle_t to_child,
                                     uintptr_t bootstrap_handle_in_child) {
    if (lp->error)
        return lp->error;

    zx_handle_t thread;
    uintptr_t sp;
    zx_status_t status = prepare_start(lp, thread_name, to_child,
                                       &thread, &sp);
    if (status != ZX_OK) {
        lp_error(lp, status, "start_injected: prepare_start() failed");
    } else {
        status = zx_thread_start(thread, lp->entry, sp,
                                 bootstrap_handle_in_child, lp->vdso_base);
        if (status != ZX_OK) {
            lp_error(lp, status, "start_injected: zx_thread_start() failed");
        }
        zx_handle_close(thread);
    }
    return status;
}

zx_status_t launchpad_go(launchpad_t* lp, zx_handle_t* proc, const char** errmsg) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = launchpad_start(lp, &h);
    if (errmsg)
        *errmsg = lp->errmsg;
    if (status == ZX_OK) {
        if (proc) {
            *proc = h;
        } else {
            zx_handle_close(h);
        }
    }
    launchpad_destroy(lp);
    return status;
}

static zx_status_t launchpad_file_load_with_vdso(launchpad_t* lp, zx_handle_t vmo) {
    launchpad_file_load(lp, vmo);
    launchpad_load_vdso(lp, ZX_HANDLE_INVALID);
    return launchpad_add_vdso_vmo(lp);
}

zx_status_t launchpad_load_from_file(launchpad_t* lp, const char* path) {
    zx_handle_t vmo;
    zx_status_t status = launchpad_vmo_from_file(path, &vmo);
    if (status == ZX_OK) {
        return launchpad_file_load_with_vdso(lp, vmo);
    } else {
        return status;
    }
}

zx_status_t launchpad_load_from_fd(launchpad_t* lp, int fd) {
    zx_handle_t vmo;
    zx_status_t status = fdio_get_vmo_clone(fd, &vmo);
    if (status == ZX_OK) {
        return launchpad_file_load_with_vdso(lp, vmo);
    } else {
        return status;
    }
}

zx_status_t launchpad_load_from_vmo(launchpad_t* lp, zx_handle_t vmo) {
    return launchpad_file_load_with_vdso(lp, vmo);
}

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
#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/param.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include "elf.h"

#define PROCARGS_BUFFER_SIZE 8192

mx_handle_t mxio_build_procargs(int args_count, char* args[],
                                int auxv_count, uintptr_t auxv[],
                                int hnds_count, mx_handle_t* handles,
                                uint32_t* ids, mx_handle_t proc) {
    union {
        uint8_t buffer[PROCARGS_BUFFER_SIZE];
        struct {
            mx_proc_args_t pargs;
            uint8_t data[PROCARGS_BUFFER_SIZE - sizeof(mx_proc_args_t)];
        } msg;
    } msgbuf;
    static_assert(sizeof(msgbuf) == sizeof(msgbuf.buffer),
                  "unexpected union layout");
    mx_handle_t h0, h1;
    mx_status_t r;
    int n;

    if (args_count < 1)
        return ERR_INVALID_ARGS;
    // The auxv must have an even number of elements, and the last
    // pair must start with a zero (AT_NULL) terminator.
    if (auxv_count < 0 || auxv_count % 2 != 0 ||
        (auxv_count > 0 && auxv[auxv_count - 2] != AT_NULL))
        return ERR_INVALID_ARGS;
    if (hnds_count < 0)
        return ERR_INVALID_ARGS;
    if (proc) {
        proc = mx_handle_duplicate(proc, MX_RIGHT_SAME_RIGHTS);
        if (proc < 0) {
            cprintf("start_process: proc duplicate failed %d\n", proc);
            return proc;
        }
        if ((handles == NULL) || (ids == NULL)) {
            return ERR_INVALID_ARGS;
        }
        handles[hnds_count] = proc;
        ids[hnds_count++] = MX_HND_TYPE_PROC_SELF;
    }

    msgbuf.msg.pargs.protocol = MX_PROCARGS_PROTOCOL;
    msgbuf.msg.pargs.version = MX_PROCARGS_VERSION;
    uint8_t* p = msgbuf.msg.data;
#define CHECK_MSGBUF_SIZE(bytes) \
    do { \
        if ((ptrdiff_t) (bytes) > &msgbuf.buffer[sizeof(msgbuf)] - p) \
            return ERR_TOO_BIG; \
    } while (0)

    CHECK_MSGBUF_SIZE(hnds_count * sizeof(ids[0]));
    msgbuf.msg.pargs.handle_info_off = p - msgbuf.buffer;
    p = mempcpy(p, ids, hnds_count * sizeof(ids[0]));

    CHECK_MSGBUF_SIZE(auxv_count * sizeof(auxv[0]));
    msgbuf.msg.pargs.aux_info_off = p - msgbuf.buffer;
    msgbuf.msg.pargs.aux_info_num = auxv_count;
    p = mempcpy(p, auxv, auxv_count * sizeof(auxv[0]));

    msgbuf.msg.pargs.args_off = p - msgbuf.buffer;
    msgbuf.msg.pargs.args_num = args_count;
    for (n = 0; n < args_count; n++) {
        size_t len = strlen(args[n]) + 1;
        CHECK_MSGBUF_SIZE(len);
        p = mempcpy(p, args[n], len);
    }

#undef CHECK_MSGBUF_SIZE

    if ((h0 = mx_message_pipe_create(&h1)) < 0) {
        return h0;
    }
    if ((r = mx_message_write(h1, msgbuf.buffer, p - msgbuf.buffer,
                                    handles, hnds_count, 0)) < 0) {
        cprintf("start_process: failed to write args %d\n", r);
        mx_handle_close(h0);
        mx_handle_close(h1);
        return r;
    }
    // we're done with our end now
    mx_handle_close(h1);
    return h0;
}

#define MAX_AUXV_COUNT (8*2)

mx_handle_t mxio_start_process_etc(const char* name, int args_count, char* args[],
                                   int hnds_count, mx_handle_t* handles, uint32_t* ids) {
    mx_handle_t h, p;
    mx_status_t r;

    if (args_count < 1)
        return ERR_INVALID_ARGS;

    if (name == NULL) {
        name = args[0];
    }
    uint32_t name_len = MIN(strlen(name), MX_MAX_NAME_LEN);

    if ((p = mx_process_create(name, name_len)) < 0) {
        return p;
    }

    uintptr_t entry = 0;
    int auxv_count = MAX_AUXV_COUNT;
    uintptr_t auxv[MAX_AUXV_COUNT];
    r = mxio_load_elf_filename(p, args[0], &auxv_count, auxv, &entry);
    if (r < 0) {
        mx_handle_close(p);
        return r;
    }

    if ((h = mxio_build_procargs(args_count, args, auxv_count, auxv,
                                 hnds_count, handles, ids, 0)) < 0) {
        mx_handle_close(p);
        return h;
    }

    if ((r = mx_process_start(p, h, entry)) < 0) {
        mx_handle_close(h);
        mx_handle_close(p);
        return r;
    }

    return p;
}

mx_status_t mxio_create_subprocess_handles(mx_handle_t* handles, uint32_t* types, size_t count) {
    mx_status_t r;
    size_t n = 0;

    if (count < MXIO_MAX_HANDLES)
        return ERR_NO_MEMORY;

    if ((r = mxio_clone_root(handles + n, types + n)) < 0) {
        return r;
    }
    n += r;
    count -= r;

    for (int fd = 0; (fd < MAX_MXIO_FD) && (count >= MXIO_MAX_HANDLES); fd++) {
        if ((r = mxio_clone_fd(fd, fd, handles + n, types + n)) <= 0) {
            continue;
        }
        n += r;
        count -= r;
    }
    return n;
}

mx_handle_t mxio_start_process(const char* name, int args_count, char* args[]) {
    // worset case slots for all fds plus root handle
    // plus a process handle possibly added by start process
    mx_handle_t hnd[(2 + MAX_MXIO_FD) * MXIO_MAX_HANDLES];
    uint32_t ids[(2 + MAX_MXIO_FD) * MXIO_MAX_HANDLES];
    mx_status_t r;

    r = mxio_create_subprocess_handles(hnd, ids, (1 + MAX_MXIO_FD) * MXIO_MAX_HANDLES);
    if (r < 0) {
        return r;
    } else {
        return mxio_start_process_etc(name, args_count, args, r, hnd, ids);
    }
}

typedef struct {
    uint8_t* data;
    size_t len;
    int fd;
} ctxt;

static ssize_t _elf_read(elf_handle_t* elf, void* buf, uintptr_t off, size_t len) {
    ctxt* c = elf->arg;
    memcpy(buf, c->data + off, len);
    return len;
}

static mx_status_t _elf_load(elf_handle_t* elf, uintptr_t vaddr, uintptr_t off, size_t len) {
    ctxt* c = elf->arg;

    mx_ssize_t ret = mx_vm_object_write(elf->vmo, c->data + off, vaddr - elf->vmo_addr, len);
    if (ret < 0 || (size_t)ret != len) {
        cprintf("failed to write\n");
        return ret;
    }

    return len;
}

mx_status_t mxio_load_elf_mem(mx_handle_t process, mx_vaddr_t* entry, void* data, size_t len) {
    mx_status_t status;
    elf_handle_t elf;
    ctxt c;
    c.data = data;
    c.len = len;
    elf_open_handle(&elf, process, _elf_read, _elf_load, &c);
    status = elf_load(&elf);
    *entry = elf.entry;
    elf_close_handle(&elf);
    return status;
}

static ssize_t _elf_read_fd(elf_handle_t* elf, void* buf, uintptr_t off, size_t len) {
    ctxt* c = elf->arg;
    if (lseek(c->fd, off, SEEK_SET) != (off_t)off)
        return ERR_IO;
    if (read(c->fd, buf, len) != (ssize_t)len)
        return ERR_IO;
    return len;
}

static mx_status_t _elf_load_fd(elf_handle_t* elf, uintptr_t vaddr, uintptr_t off, size_t len) {
    uint8_t tmp[4096];
    ctxt* c = elf->arg;

    size_t save = len;

    if (lseek(c->fd, off, SEEK_SET) != (off_t)off)
        return ERR_IO;

    while (len > 0) {
        size_t xfer = (len > 4096) ? 4096 : len;
        if (read(c->fd, tmp, xfer) != (ssize_t)xfer)
            return ERR_IO;

        mx_ssize_t ret = mx_vm_object_write(elf->vmo, tmp, vaddr - elf->vmo_addr, xfer);
        if (ret < 0 || (size_t)ret != xfer) {
            return ret;
        }

        len -= xfer;
        vaddr += xfer;
    }
    return save;
}

mx_status_t mxio_load_elf_fd(mx_handle_t process, mx_vaddr_t* entry, int fd) {
    mx_status_t status;
    elf_handle_t elf;
    ctxt c;
    c.fd = fd;
    elf_open_handle(&elf, process, _elf_read_fd, _elf_load_fd, &c);
    status = elf_load(&elf);
    *entry = elf.entry;
    elf_close_handle(&elf);
    return status;
}

static mx_status_t load_elf_filename(ctxt* c, elf_handle_t* elf,
                                     mx_handle_t process,
                                     const char* filename) {
    mx_status_t status = elf_open_handle(elf, process,
                                         _elf_read_fd, _elf_load_fd, c);
    if (status == NO_ERROR) {
        c->fd = open(filename, O_RDONLY);
        if (c->fd < 0)
            return ERR_IO;
        status = elf_load(elf);
    }
    return status;
}

mx_status_t mxio_load_elf_filename(mx_handle_t process, const char* filename,
                                   int* auxv_count, uintptr_t auxv[],
                                   mx_vaddr_t* entry) {

    ctxt c;
    elf_handle_t elf;

    mx_status_t status = load_elf_filename(&c, &elf, process, filename);
    if (status != NO_ERROR)
        goto fail;

    int auxv_idx = 0;
    if (elf.phdr_vaddr != 0) {
        if (auxv_idx + 1 < *auxv_count) {
            auxv[auxv_idx++] = AT_PHDR;
            auxv[auxv_idx++] = elf.phdr_vaddr + elf.load_bias;
        }
        if (auxv_idx + 1 < *auxv_count) {
            auxv[auxv_idx++] = AT_PHENT;
            auxv[auxv_idx++] = sizeof(elf.pheaders[0]);
        }
        if (auxv_idx + 1 < *auxv_count) {
            auxv[auxv_idx++] = AT_PHNUM;
            auxv[auxv_idx++] = elf.eheader.e_phnum;
        }
    }

    *entry = elf.entry + elf.load_bias;

    if (status == NO_ERROR && elf.interp_len != 0) {
        // There is a PT_INTERP segment, so load the interpreter.

        // First, we read the segment's contents: the interpreter file name.
        char* interp = malloc(elf.interp_len);
        if (interp == NULL) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        ssize_t read_len = _elf_read_fd(&elf, interp,
                                        elf.interp_offset, elf.interp_len);
        if (read_len != (ssize_t)elf.interp_len) {
            free(interp);
            status = read_len < 0 ? read_len : ERR_IO;
            goto fail;
        }

        // This is supposed to be a file name, so it better be a proper string.
        if (interp[elf.interp_len - 1] != '\0') {
            free(interp);
            status = ERR_BAD_PATH;
            goto fail;
        }

        ctxt interp_ctxt;
        elf_handle_t interp_elf;
        status = load_elf_filename(&interp_ctxt, &interp_elf, process, interp);
        free(interp);

        if (status == NO_ERROR) {
            if (auxv_idx + 1 < *auxv_count) {
                auxv[auxv_idx++] = AT_BASE;
                auxv[auxv_idx++] = interp_elf.load_bias;
            }
            if (auxv_idx + 1 < *auxv_count) {
                auxv[auxv_idx++] = AT_ENTRY;
                auxv[auxv_idx++] = elf.entry + elf.load_bias;
            }
            *entry = interp_elf.entry + interp_elf.load_bias;
        }

        elf_close_handle(&interp_elf);
    }

    if (auxv_idx != 0) {
        if (auxv_idx + 1 < *auxv_count) {
            auxv[auxv_idx++] = AT_NULL;
            auxv[auxv_idx++] = 0;
            *auxv_count = auxv_idx;
        } else {
            status = ERR_NOT_ENOUGH_BUFFER;
        }
    }

fail:
    elf_close_handle(&elf);
    close(c.fd);
    return status;
}

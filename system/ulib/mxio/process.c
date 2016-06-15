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

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
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

mx_handle_t mxio_build_procargs(int args_count, char* args[], int hnds_count,
                                mx_handle_t* handles, uint32_t* ids, mx_handle_t proc) {
    uint8_t data[8192];
    mx_proc_args_t pargs;
    mx_handle_t h0, h1;
    mx_status_t r;
    off_t off = sizeof(pargs);
    int n;


    if (args_count < 1) return ERR_INVALID_ARGS;
    if (hnds_count < 0) return ERR_INVALID_ARGS;
    if (proc) {
        proc = _magenta_handle_duplicate(proc);
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
    //TODO: bounds checking
    pargs.protocol = MX_PROCARGS_PROTOCOL;
    pargs.version = MX_PROCARGS_VERSION;
    pargs.handle_info_off = off;
    memcpy(data + off, ids, hnds_count * sizeof(uint32_t));
    off += hnds_count * sizeof(uint32_t);
    pargs.args_off = off;
    pargs.args_num = args_count;
    for (n = 0; n < args_count; n++) {
        strcpy((void*)data + off, args[n]);
        off += strlen(args[n]) + 1;
    }
    memcpy(data, &pargs, sizeof(pargs));

    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
        return h0;
    }
    if ((r = _magenta_message_write(h1, data, off, handles, hnds_count, 0)) < 0) {
        cprintf("start_process: failed to write args %d\n", r);
        _magenta_handle_close(h0);
        _magenta_handle_close(h1);
        return r;
    }
    // we're done with our end now
    _magenta_handle_close(h1);
    return h0;
}

mx_handle_t mxio_start_process_etc(int args_count, char* args[], int hnds_count,
                                   mx_handle_t* handles, uint32_t* ids) {
    uintptr_t entry = 0;
    mx_handle_t h, p;
    mx_status_t r;

    if (args_count < 1) return ERR_INVALID_ARGS;

    char* path = args[0];
    uint32_t path_len = MIN(strlen(path), MX_MAX_NAME_LEN);

    if ((p = _magenta_process_create(path, path_len)) < 0) {
        return p;
    }
    if ((h = mxio_build_procargs(args_count, args, hnds_count, handles, ids, 0)) < 0) {
        _magenta_handle_close(p);
        return h;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        r = ERR_IO;
        goto fail;
    }
    r = mxio_load_elf_fd(p, &entry, fd);
    close(fd);
    if (r < 0) {
        goto fail;
    }
    if ((r = _magenta_process_start(p, h, entry)) < 0) {
    fail:
        _magenta_handle_close(h);
        _magenta_handle_close(p);
        return r;
    }
    return p;
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

    mx_ssize_t ret = _magenta_vm_object_write(elf->vmo, c->data + off, vaddr - elf->vmo_addr, len);
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
    if (lseek(c->fd, off, SEEK_SET) != (off_t)off) return ERR_IO;
    if (read(c->fd, buf, len) != (ssize_t)len) return ERR_IO;
    return len;
}

static mx_status_t _elf_load_fd(elf_handle_t* elf, uintptr_t vaddr, uintptr_t off, size_t len) {
    uint8_t tmp[4096];
    ctxt* c = elf->arg;

    size_t save = len;

    if (lseek(c->fd, off, SEEK_SET) != (off_t)off) return ERR_IO;

    while (len > 0) {
        size_t xfer = (len > 4096) ? 4096 : len;
        if (read(c->fd, tmp, xfer) != (ssize_t)xfer) return ERR_IO;

        mx_ssize_t ret = _magenta_vm_object_write(elf->vmo, tmp, vaddr - elf->vmo_addr, xfer);
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

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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include <magenta/syscalls.h>
#include <magenta/processargs.h>

static size_t devmgr_off = 0;
static size_t devmgr_len = 0;
static size_t end_off = 0;

static void callback(const char* fn, size_t off, size_t len) {
    //cprintf("bootfs: %s @%zd (%zd bytes)\n", fn, off, len);
    if (!strcmp(fn, "bin/devmgr")) {
        devmgr_off = off;
        devmgr_len = len;
    }
    off += len;
    if (off > end_off) {
        end_off = off;
    }
}

static const char* args[1] = {
    "bin/devmgr",
};

static void* arg;

void* __libc_intercept_arg(void* _arg) {
    arg = _arg;
    return NULL;
}

int main(int argc, char** argv) {
    mx_handle_t bootfs_vmo = (mx_handle_t)(uintptr_t)arg;
    uint64_t bootfs_size;
    uintptr_t bootfs_val;

    mx_status_t status = _magenta_vm_object_get_size(bootfs_vmo, &bootfs_size);
    if (status < 0) {
        cprintf("userboot: failed to get bootfs size (%d)\n", status);
        return -1;
    }
    status = _magenta_process_vm_map(
        0, bootfs_vmo, 0, bootfs_size, &bootfs_val, MX_VM_FLAG_PERM_READ);
    if (status < 0) {
        cprintf("userboot: failed to map bootfs (%d)\n", status);
        return -1;
    }
    void* bootfs = (void*)bootfs_val;

    cprintf("userboot: starting...\n");
    bootfs_parse(bootfs, 32768, callback);
    if (devmgr_off == 0) {
        cprintf("userboot: error: bin/devmgr not found\n");
        return -1;
    }

    uint8_t* devmgr = ((uint8_t*)bootfs) + devmgr_off;
    mx_handle_t proc;
    mx_vaddr_t entry;
    mx_handle_t h;
    mx_status_t r;

    const char pname[] = "devmgr";

    if ((proc = _magenta_process_create(pname, sizeof(pname))) < 0)
        return proc;
    if ((r = mxio_load_elf_mem(proc, &entry, devmgr, devmgr_len))) {
        cprintf("userboot: elf load of devmgr failed %d\n", r);
        return r;
    }

    mx_handle_t handles[1] = {bootfs_vmo};
    uint32_t ids[1] = {MX_HND_INFO(MX_HND_TYPE_USER0,0)};

    if ((h = mxio_build_procargs(1, (char**)args, 1, handles, ids, 0)) < 0) {
        cprintf("userboot: failed to build devmgr args %d\n", h);
        return h;
    }
    _magenta_process_start(proc, h, entry);

    return 0;
}


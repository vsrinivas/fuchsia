// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/process.h>

#include <stddef.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

// TODO: allocate via vmo perhaps?
static char __proc_data__[4096];
mx_proc_info_t __proc_info__;

mx_proc_info_t* mxr_process_get_info(void) {
    return &__proc_info__;
}

mx_handle_t mxr_process_get_handle(uint32_t info) {
    mx_proc_info_t* pi = &__proc_info__;
    mx_handle_t h = 0;

    // TODO: locking
    for (int n = 0; n < pi->handle_count; n++) {
        if (pi->handle_info[n] == info) {
            h = pi->handle[n];
            pi->handle[n] = 0;
            pi->handle_info[n] = 0;
            break;
        }
    }
    return h;
}

static void unpack_strings(uint32_t c, char** v, char* p) {
    for (uint32_t i = 0; i < c; ++i) {
        v[i] = p;
        do {
            ++p;
        } while (p[-1] != '\0');
    }
}

mx_proc_info_t* mxr_process_parse_args(void* arg) {
    char* data = __proc_data__;
    int avail = sizeof(__proc_data__);
    mx_proc_info_t* pi = &__proc_info__;
    mx_handle_t h = (uintptr_t)arg;
    mx_status_t r;
    uint32_t dsz = 0, hsz = 0;
    char* msg = NULL;
    mx_proc_args_t* pargs;
    char** argv;
    char** envp;

    // Avoid calling memset so as not to depend on libc.
    {
        const mx_proc_info_t zero = {};
        *pi = zero;
    }

    // discover size of message and handles, allocate space
    r = mx_message_read(h, NULL, &dsz, NULL, &hsz, 0);
    if (r == ERR_NOT_ENOUGH_BUFFER) {
        int need = dsz + hsz * sizeof(mx_handle_t);
        need = (need + 7) & (~7);
        if (need > avail)
            return pi;
        msg = data;
        data += need;
        avail -= need;
        pi->handle = (mx_handle_t*)msg;
        pi->handle_count = hsz;
        msg += sizeof(mx_handle_t) * hsz;
    } else {
        return pi;
    }

    // obtain message and handles
    r = mx_message_read(h, msg, &dsz, pi->handle, &hsz, 0);
    mx_handle_close(h);
    if (r < 0) {
        return pi;
    }

    // validate proc args
    pi->proc_args = pargs = (mx_proc_args_t*)msg;
    if (dsz < sizeof(*pargs))
        return pi;
    if (pargs->protocol != MX_PROCARGS_PROTOCOL)
        return pi;

    if (pargs->handle_info_off > dsz ||
        (dsz - pargs->handle_info_off) / sizeof(uint32_t) < hsz)
        return pi;
    pi->handle_info = (uint32_t*)(msg + pargs->handle_info_off);

    // extract arguments
    if ((sizeof(char*) * pargs->args_num) > (unsigned)avail)
        return pi;
    argv = (void*)data;
    data += sizeof(char*) * pargs->args_num;
    avail -= sizeof(char*) * pargs->args_num;
    unpack_strings(pargs->args_num, argv, msg + pargs->args_off);

    // extract environment strings
    if ((sizeof(char*) * pargs->environ_num) > (unsigned)avail)
        return pi;
    envp = (void*)data;
    data += sizeof(char*) * pargs->environ_num;
    avail -= sizeof(char*) * pargs->environ_num;
    unpack_strings(pargs->environ_num, envp, msg + pargs->environ_off);

    pi->magic = MX_PROC_INFO_MAGIC;
    pi->version = MX_PROC_INFO_VERSION;
    pi->argc = pargs->args_num;
    pi->argv = argv;
    pi->envc = pargs->environ_num;
    pi->envp = envp;
    return pi;
}

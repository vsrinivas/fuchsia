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

#include <runtime/process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

// TODO: allocate via vmo perhaps?
static char __proc_data__[4096];
static mx_proc_info_t __proc_info__;

mx_proc_info_t* mxr_process_get_info(void) {
    return &__proc_info__;
}

mx_handle_t mxr_process_get_handle(uint32_t info) {
    mx_proc_info_t* pi = &__proc_info__;
    mx_handle_t h = 0;

    // TODO: locking once mxr_mutex exists
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
    uint32_t n;

    memset(pi, 0, sizeof(*pi));

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

    if (pargs->aux_info_off > dsz ||
        (dsz - pargs->aux_info_off) / sizeof(uintptr_t) < pargs->aux_info_num)
        return pi;
    if (pargs->aux_info_num > 0) {
        pi->auxv = (uintptr_t*)(msg + pargs->aux_info_off);
        // The auxv must have an even number of elements, and the last
        // pair must start with a zero (AT_NULL) terminator.
        if (pargs->aux_info_num % 2 != 0 ||
            pi->auxv[pargs->aux_info_num - 2] != 0)
            return pi;
    }

    // extract arguments
    if ((sizeof(char*) * pargs->args_num) > (unsigned)avail)
        return pi;
    argv = (void*)data;

    msg = msg + pargs->args_off;
    for (n = 0; n < pargs->args_num; n++) {
        argv[n] = msg;
        while (*msg)
            msg++;
        msg++;
    }

    pi->magic = MX_PROC_INFO_MAGIC;
    pi->version = MX_PROC_INFO_VERSION;
    pi->argc = pargs->args_num;
    pi->argv = argv;
    return pi;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/processargs.h>

#include <zircon/syscalls.h>
#include <string.h>

// TODO(mcgrathr): Is there a better error code to use for marshalling
// protocol violations?
#define MALFORMED ZX_ERR_INVALID_ARGS

zx_status_t zxr_processargs_read(zx_handle_t bootstrap,
                                 void* buffer, uint32_t nbytes,
                                 zx_handle_t handles[], uint32_t nhandles,
                                 zx_proc_args_t** pargs,
                                 uint32_t** handle_info) {
    if (nbytes < sizeof(zx_proc_args_t))
        return ZX_ERR_INVALID_ARGS;
    if ((uintptr_t)buffer % alignof(zx_proc_args_t) != 0)
        return ZX_ERR_INVALID_ARGS;

    uint32_t got_bytes = 0;
    uint32_t got_handles = 0;
    zx_status_t status = _zx_channel_read(bootstrap, 0, buffer, handles, nbytes,
                                          nhandles, &got_bytes, &got_handles);
    if (status != ZX_OK)
        return status;
    if (got_bytes != nbytes || got_handles != nhandles)
        return ZX_ERR_INVALID_ARGS;

    zx_proc_args_t* const pa = buffer;

    if (pa->protocol != ZX_PROCARGS_PROTOCOL ||
        pa->version != ZX_PROCARGS_VERSION)
        return MALFORMED;

    if (pa->handle_info_off < sizeof(*pa) ||
        pa->handle_info_off % alignof(uint32_t) != 0 ||
        pa->handle_info_off > nbytes ||
        (nbytes - pa->handle_info_off) / sizeof(uint32_t) < nhandles)
        return MALFORMED;

    if (pa->args_num > 0 && (pa->args_off < sizeof(*pa) ||
                             pa->args_off > nbytes ||
                             (nbytes - pa->args_off) < pa->args_num))
        return MALFORMED;

    if (pa->environ_num > 0 && (pa->environ_off < sizeof(*pa) ||
                                pa->environ_off > nbytes ||
                                (nbytes - pa->environ_off) < pa->environ_num))
        return MALFORMED;

    *pargs = pa;
    *handle_info = (void*)&((uint8_t*)buffer)[pa->handle_info_off];
    return ZX_OK;
}

static zx_status_t unpack_strings(char* buffer, uint32_t bytes, char* result[],
                                  uint32_t off, uint32_t num) {
    char* p = &buffer[off];
    for (uint32_t i = 0; i < num; ++i) {
        result[i] = p;
        do {
            if (p >= &buffer[bytes])
                return MALFORMED;
        } while (*p++ != '\0');
    }
    result[num] = NULL;
    return ZX_OK;
}

zx_status_t zxr_processargs_strings(void* msg, uint32_t bytes,
                                    char* argv[], char* envp[], char* names[]) {
    zx_proc_args_t* const pa = msg;
    zx_status_t status = ZX_OK;
    if (argv != NULL) {
        status = unpack_strings(msg, bytes, argv, pa->args_off, pa->args_num);
    }
    if (envp != NULL && status == ZX_OK) {
        status = unpack_strings(msg, bytes, envp,
                                pa->environ_off, pa->environ_num);
    }
    if (names != NULL && status == ZX_OK) {
        status = unpack_strings(msg, bytes, names, pa->names_off, pa->names_num);
    }
    return status;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zbi.h"

#include <libzbi/zbi-zx.h>
#include <stdio.h>
#include <zircon/status.h>

zx_status_t netboot_prepare_zbi(zx_handle_t nbkernel_vmo,
                                zx_handle_t nbbootdata_vmo,
                                const uint8_t* cmdline, uint32_t cmdline_size,
                                dmctl_mexec_args_t* args) {
    zbi::ZbiVMO kernel, data;

    if (nbkernel_vmo == ZX_HANDLE_INVALID) {
        printf("netbootloader: no kernel!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (nbbootdata_vmo == ZX_HANDLE_INVALID) {
        // Split the complete ZBI into its kernel and data parts.
        zbi::ZbiVMO zbi;
        auto status = zbi.Init(zx::vmo{nbkernel_vmo});
        if (status != ZX_OK) {
            printf("netbootloader: can't map complete ZBI: %d (%s)\n",
                   status, zx_status_get_string(status));
            return status;
        }
        auto result = zbi.SplitComplete(&kernel, &data);
        if (result != ZBI_RESULT_OK) {
            printf("netbootloader: invalid complete ZBI: %d\n", result);
            return ZX_ERR_INTERNAL;
        }
    } else {
        // Old-style boot with separate kernel and data ZBIs.
        printf("netbootloader: old-style boot is deprecated;"
               " switch to complete ZBI!\n");
        auto status = kernel.Init(zx::vmo{nbkernel_vmo});
        if (status != ZX_OK) {
            printf("netbootloader: can't map kernel ZBI: %d (%s)\n",
                   status, zx_status_get_string(status));
            return status;
        }
        status = data.Init(zx::vmo{nbbootdata_vmo});
        if (status != ZX_OK) {
            printf("netbootloader: can't map kernel ZBI: %d (%s)\n",
                   status, zx_status_get_string(status));
            return status;
        }
    }

    if (cmdline_size > 0) {
        auto result = data.AppendSection(cmdline_size, ZBI_TYPE_CMDLINE, 0, 0,
                                         cmdline);
        if (result != ZBI_RESULT_OK) {
            printf("netbootloader: failed to append command line: %d\n",
                   result);
            return ZX_ERR_INTERNAL;
        }
    }

    printf("netbootloader: kernel ZBI %#x bytes data ZBI %#x bytes\n",
           kernel.Length(), data.Length());

    args->kernel = kernel.Release().release();
    args->bootdata = data.Release().release();
    return ZX_OK;
}

// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vmo-utils.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

// Reads the mx_info_vmo_t entries for the process.
// Caller is responsible for the |out_vmos| pointer.
mx_status_t get_vmos(mx_handle_t process,
                     mx_info_vmo_t** out_vmos, size_t* out_count,
                     size_t* out_avail) {
    size_t count = 4096; // Should be more than enough.
    mx_info_vmo_t* vmos = NULL;
    int pass = 3;
    while (true) {
        vmos = (mx_info_vmo_t*)realloc(vmos, count * sizeof(mx_info_vmo_t));

        size_t actual;
        size_t avail;
        mx_status_t s = mx_object_get_info(process, MX_INFO_PROCESS_VMOS,
                                           vmos, count * sizeof(mx_info_vmo_t),
                                           &actual, &avail);
        if (s != MX_OK) {
            free(vmos);
            return s;
        }
        if (actual < avail && pass-- > 0) {
            count = (avail * 10) / 9;
            continue;
        }
        *out_vmos = vmos;
        *out_count = actual;
        *out_avail = avail;
        return MX_OK;
    }
}

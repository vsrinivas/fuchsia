// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-utils.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

// Reads the zx_info_vmo_t entries for the process.
// Caller is responsible for the |out_vmos| pointer.
zx_status_t get_vmos(zx_handle_t process, zx_info_vmo_t** out_vmos, size_t* out_count,
                     size_t* out_avail) {
  size_t count = 4096;  // Should be more than enough.
  zx_info_vmo_t* vmos = NULL;
  int pass = 3;
  while (true) {
    vmos = (zx_info_vmo_t*)realloc(vmos, count * sizeof(zx_info_vmo_t));

    size_t actual;
    size_t avail;
    zx_status_t s = zx_object_get_info(process, ZX_INFO_PROCESS_VMOS, vmos,
                                       count * sizeof(zx_info_vmo_t), &actual, &avail);
    if (s != ZX_OK) {
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
    return ZX_OK;
  }
}

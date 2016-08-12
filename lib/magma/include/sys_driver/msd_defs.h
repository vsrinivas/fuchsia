// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MSD_DEFS_H_
#define _MSD_DEFS_H_

#include "common/magma_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint32_t msd_client_id;

struct msd_exec_buffer {
    uint32_t placeholder;
};

#if defined(__cplusplus)
}
#endif

#endif /* _MSD_DEFS_H_ */

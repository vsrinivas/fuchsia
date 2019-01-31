// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "utils-impl.h"

struct inspector_dsoinfo {
    struct inspector_dsoinfo* next;
    zx_vaddr_t base;
    char buildid[MAX_BUILDID_SIZE * 2 + 1];
    bool debug_file_tried;
    zx_status_t debug_file_status;
    char* debug_file;
    char name[];
};

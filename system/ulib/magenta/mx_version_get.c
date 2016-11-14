// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include "git-version.h"

static const char kMagentaVersion[] = MAGENTA_GIT_REV;

mx_status_t _mx_version_get(char* version, uint32_t version_len) {
    if (version_len < sizeof(kMagentaVersion))
        return ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < sizeof(kMagentaVersion); ++i)
        version[i] = kMagentaVersion[i];
    return NO_ERROR;
}

__typeof(mx_version_get) mx_version_get
    __attribute__((weak, alias("_mx_version_get")));

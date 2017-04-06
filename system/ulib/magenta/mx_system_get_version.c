// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include "config-buildid.h"

static const char kMagentaVersion[] = BUILDID;

mx_status_t _mx_system_get_version(char* version, uint32_t version_len) {
    if (version_len < sizeof(kMagentaVersion))
        return ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < sizeof(kMagentaVersion); ++i)
        version[i] = kMagentaVersion[i];
    return NO_ERROR;
}

__typeof(mx_system_get_version) mx_system_get_version
    __attribute__((weak, alias("_mx_system_get_version")));

// Deprecated compatibility aliases.
__typeof(mx_system_get_version) _mx_version_get
    __attribute__((weak, alias("_mx_system_get_version")));
__typeof(mx_system_get_version) mx_version_get
    __attribute__((weak, alias("_mx_system_get_version")));

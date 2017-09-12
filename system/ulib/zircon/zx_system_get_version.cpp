// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "config-buildid.h"
#include "private.h"

static const char kZirconVersion[] = BUILDID;

zx_status_t _zx_system_get_version(char* version, uint32_t version_len) {
    if (version_len < sizeof(kZirconVersion))
        return ZX_ERR_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < sizeof(kZirconVersion); ++i)
        version[i] = kZirconVersion[i];
    return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_version);

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These functions are designed to be nonbreaking stubs of libdrm functions so all
// so all the random places where mesa uses them can build

#include "magma_util/dlog.h"
#include "xf86drm.h"
#include <stdio.h>

int drmIoctl(int fd, unsigned long request, void* arg)
{
    DLOG("Warning: Mesa hitting drmIoctl stub, this may cause problems");
    return -1;
}
void* drmGetHashTable(void)
{
    DLOG("Warning: Mesa hitting drmGetHashTable stub, this may cause problems");
    return 0;
}
drmHashEntry* drmGetEntry(int fd)
{
    DLOG("Warning: Mesa hitting drmGetEntry stub, this may cause problems");
    return 0;
}

int drmCommandNone(int fd, unsigned long drmCommandIndex)
{
    DLOG("Warning: Mesa hitting drmCommandNone stub, this may cause problems");
    return -1;
}

int drmCommandWriteRead(int fd, unsigned long drmCommandIndex, void* data, unsigned long size)
{
    DLOG("Warning: Mesa hitting drmCommandWriteRead stub, this may cause problems");
    return -1;
}

int drmAuthMagic(int fd, drm_magic_t magic)
{
    DLOG("Warning: Mesa hitting drmAuthMagic stub, this may cause problems");
    return -1;
}
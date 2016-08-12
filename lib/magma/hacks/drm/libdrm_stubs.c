// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These functions are designed to be nonbreaking stubs of libdrm functions so all
// so all the random places where mesa uses them can build

#include "xf86drm.h"
#include <stdio.h>

int drmIoctl(int fd, unsigned long request, void* arg)
{
    printf("Warning: Mesa hitting drmIoctl stub, this may cause problems\n");
    return -1;
}
void* drmGetHashTable(void)
{
    printf("Warning: Mesa hitting drmGetHashTable stub, this may cause problems\n");
    return 0;
}
drmHashEntry* drmGetEntry(int fd)
{
    printf("Warning: Mesa hitting drmGetEntry stub, this may cause problems\n");
    return 0;
}

int drmCommandNone(int fd, unsigned long drmCommandIndex)
{
    printf("Warning: Mesa hitting drmCommandNone stub, this may cause problems\n");
    return -1;
}

// int drmCommandRead(int fd, unsigned long drmCommandIndex,
//                                     void *data, unsigned long size){
// 	printf("Warning: Mesa hitting drmCommandRead stub, this may cause problems\n");
// 	return -1;
// }

int drmCommandWriteRead(int fd, unsigned long drmCommandIndex, void* data, unsigned long size)
{
    printf("Warning: Mesa hitting drmCommandWriteRead stub, this may cause problems\n");
    return -1;
}

int drmAuthMagic(int fd, drm_magic_t magic)
{
    printf("Warning: Mesa hitting drmAuthMagic stub, this may cause problems\n");
    return -1;
}
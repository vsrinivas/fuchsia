// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These functions are designed to be nonbreaking stubs of libdl functions so all
// so all the random places where mesa uses them can build

#include <stdio.h>

void* dlopen(const char* filename, int flag)
{
    fprintf(stderr, "Warning: Mesa hitting dlopen stub, this may cause problems\n");
    return NULL;
}

char* dlerror(void)
{
    fprintf(stderr, "Warning: Mesa hitting dlerror stub, this may cause problems\n");
    return NULL;
}

void* dlsym(void* handle, const char* symbol)
{
    fprintf(stderr, "Warning: Mesa hitting dlsym stub, this may cause problems\n");
    return NULL;
}

int dlclose(void* handle)
{
    fprintf(stderr, "Warning: Mesa hitting dlclose stub, this may cause problems\n");
    return 0;
}

int dladdr(void* addr, void* info)
{
    fprintf(stderr, "Warning: Mesa hitting dladdr stub, this may cause problems\n");
    return 0;
}

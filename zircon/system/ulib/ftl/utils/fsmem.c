// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inc/config.h>
#include <fsprivate.h>
#include <stdlib.h>
#include <string.h>
#include <sys.h>

// Wrapper for malloc()
void* FsMalloc(size_t size) {
    void* mem = malloc(size);
    if (mem == NULL)
        FsError(ENOMEM);

    return mem;
}

// Wrapper for calloc()
void* FsCalloc(size_t nitems, size_t size) {
    void* mem = calloc(nitems, size);
    if (mem == NULL)
        FsError(ENOMEM);

    return mem;
}

// Wrapper for aalloc()
void* FsAalloc(size_t size) {
    void* mem = aalloc(size);
    if (mem == NULL)
        FsError(ENOMEM);

    return mem;
}

// Wrapper for free()
void FsFree(void* ptr) {
    free(ptr);
}

// Wrapper for free_clear()
void FsFreeClear(void* ptr) {
    free_clear(ptr);
}

// Wrapper for afree_clear()
void FsAfreeClear(void* ptr) {
    afree_clear(ptr);
}

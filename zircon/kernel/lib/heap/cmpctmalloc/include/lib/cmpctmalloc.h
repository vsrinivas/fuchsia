// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <stdbool.h>
#include <stddef.h>

#include <zircon/compiler.h>

DECLARE_SINGLETON_MUTEX(TheHeapLock);

void* cmpct_alloc(size_t) __TA_EXCLUDES(TheHeapLock::Get());
void* cmpct_realloc(void*, size_t) __TA_EXCLUDES(TheHeapLock::Get());
void cmpct_free(void*) __TA_EXCLUDES(TheHeapLock::Get());
void* cmpct_memalign(size_t size, size_t alignment) __TA_EXCLUDES(TheHeapLock::Get());

void cmpct_init(void) __TA_EXCLUDES(TheHeapLock::Get());
void cmpct_dump(bool panic_time) __TA_EXCLUDES(TheHeapLock::Get());
void cmpct_get_info(size_t* size_bytes, size_t* free_bytes) __TA_EXCLUDES(TheHeapLock::Get());
void cmpct_test(void) __TA_EXCLUDES(TheHeapLock::Get());
void cmpct_trim(void) __TA_EXCLUDES(TheHeapLock::Get());

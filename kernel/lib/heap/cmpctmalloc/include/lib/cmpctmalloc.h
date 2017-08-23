// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <magenta/compiler.h>

__BEGIN_CDECLS

void* cmpct_alloc(size_t);
void* cmpct_realloc(void*, size_t);
void cmpct_free(void*);
void* cmpct_memalign(size_t size, size_t alignment);

void cmpct_init(void);
void cmpct_dump(bool panic_time);
void cmpct_get_info(size_t* size_bytes, size_t* free_bytes);
void cmpct_test(void);
void cmpct_trim(void);

__END_CDECLS

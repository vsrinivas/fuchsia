// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <stdarg.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

void platform_dputs(const char* str, size_t len);
int platform_dgetc(char *c, bool wait);
static inline void platform_dputc(char c) {
    platform_dputs(&c, 1);
}

// Should be available even if the system has panicked.
void platform_pputc(char c);
int platform_pgetc(char *c, bool wait);

__END_CDECLS

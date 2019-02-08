// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

#define RAND_MAX (0x7fffffff)

int rand(void);
void srand(unsigned int seed);

// Note; rand_r deviates a small amount from the typical glibc definition.
// Instead of taking an unsigned int as the core state, it demands a 64 bit
// integer state/seed instead.
int rand_r(uint64_t* seed);

__END_CDECLS

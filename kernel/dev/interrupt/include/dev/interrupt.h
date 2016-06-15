// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <stdbool.h>
#include <sys/types.h>

__BEGIN_CDECLS

status_t mask_interrupt(unsigned int vector);
status_t unmask_interrupt(unsigned int vector);

typedef enum handler_return (*int_handler)(void* arg);

void register_int_handler(unsigned int vector, int_handler handler, void* arg);

bool is_valid_interrupt(unsigned int vector, uint32_t flags);

unsigned int remap_interrupt(unsigned int vector);

__END_CDECLS

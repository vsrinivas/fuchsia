// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <list.h>
#include <sys/types.h>
#include <zircon/compiler.h>

/* LK specific calls to register to get input/output of the main console */

__BEGIN_CDECLS

typedef struct __print_callback print_callback_t;
struct __print_callback {
    struct list_node entry;
    void (*print)(print_callback_t* cb, const char* str, size_t len);
    void* context;
};

/* register callback to receive debug prints */
void register_print_callback(print_callback_t* cb);
void unregister_print_callback(print_callback_t* cb);

/* back doors to directly write to the kernel serial and console */
void __kernel_serial_write(const char* str, size_t len);
void __kernel_console_write(const char* str, size_t len);

/* path from printf() to kernel debug output */
int __printf_output_func(const char* s, size_t len, void* state);

__END_CDECLS

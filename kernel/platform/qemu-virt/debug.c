// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <reg.h>
#include <stdio.h>
#include <kernel/thread.h>
#include <dev/uart.h>
#include <platform/debug.h>
#include <platform/qemu-virt.h>
#include <target/debugconfig.h>
#include <reg.h>

void platform_dputs(const char* str, size_t len)
{
    while (len-- > 0) {
        char c = *str++;
        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc(c);
    }
}

int platform_dgetc(char *c, bool wait)
{
    int ret = uart_getc(wait);
    if (ret == -1)
        return -1;
    *c = ret;
    return 0;
}

void platform_pputc(char c)
{
    if (c == '\n')
        uart_pputc('\r');
    uart_pputc(c);
}

int platform_pgetc(char *c, bool wait)
{
    int ret = uart_pgetc();
    if (ret < 0)
        return ret;
    *c = ret;
    return 0;
}


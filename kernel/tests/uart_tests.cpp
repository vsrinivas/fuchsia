// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>

#include <lib/debuglog.h>

#include <zircon/types.h>

#include <rand.h>

static char buf[1048];

static void uart_blocking_print_test(void)
{
    int count;

    for (count = 0 ; count < 5 ; count++) {
        snprintf(buf, sizeof(buf), "Blocking Test Count %d (FIRST LINE)\n"
                 "AND THIS SHOULD BE THE SECOND LINE Count %d\n",
                 count, count);
        dlog_serial_write(buf, strlen(buf));
    }
}

static void uart_nonblocking_print_test(void)
{
    int count;

    for (count = 0 ; count < 5 ; count++) {
        snprintf(buf, sizeof(buf), "NON-Blocking Test Count %d (FIRST LINE)\n"
                 "AND THIS SHOULD BE THE SECOND LINE Count %d\n",
                 count, count);
        __kernel_serial_write(buf, strlen(buf));
    }
}

static void uart_print_lots_of_lines(bool block)
{
    int count;
    char *s = buf;
    int i;
#define TOTAL_LINES   1024
#define MAX_SINGLE_LINE_LEN 128
#define MIN_SINGLE_LINE_LEN  80

    for (count = 0 ; count < TOTAL_LINES ; count++) {
        if (block)
            snprintf(buf, 1024, "BLOCK LINE: %d ", count);
        else
            snprintf(buf, 1024, "NON-BLOCK LINE: %d ", count);
        // Pick a random line length between 80 and 128
        i = (rand() % (MAX_SINGLE_LINE_LEN - MIN_SINGLE_LINE_LEN));
        i += MIN_SINGLE_LINE_LEN;
        i -= (int)strlen(buf); // Less what we've already snprintf'ed above
        s = buf + strlen(buf);
        while (i--) {
            if ((rand() % 2) == 0)
                *s++ = (char)((int)'a' + (int)(rand() % 26));
            else
                *s++ = (char)((int)'A' + (int)(rand() % 26));
        }
        *s++ = '\n';
        *s = '\0';
        if (block)
            dlog_serial_write(buf, strlen(buf));
        else
            __kernel_serial_write(buf, strlen(buf));
    }
}

void uart_tests(void) {
    // Print out a few short lines, to test '\n' behavior
    uart_blocking_print_test();
    uart_nonblocking_print_test();
    // Print a few long lines to test bulk output,
    // both block and non-blocking
    int i = 100;

    while (i--) {
        uart_print_lots_of_lines(true);
        uart_print_lots_of_lines(false);
        printf("Printed Count = %d\n", 100 - i);
    }
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <runtime/status.h>

#include "utils.h"

// Same as basename, except will not modify |file|.
// This assumes there are no trailing /s. If there are then |file| is returned
// as is.

const char* cl_basename(const char* s) {
    // This implementation is copied from musl's basename.c.
    size_t i;
    if (!s || !*s)
        return ".";
    i = strlen(s) - 1;
    if (i > 0 && s[i] == '/')
        return s;
    for (; i && s[i - 1] != '/'; i--)
        ;
    return s + i;
}

void do_print_error(const char* file, int line, const char* fmt, ...) {
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "crashlogger: %s:%d: ", base, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void do_print_mx_error(const char* file, int line, const char* what, mx_status_t status) {
    do_print_error(file, line, "%s: %d (%s)",
                   what, status, mx_strstatus(status));
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <magenta/syscalls.h>
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

int debug_level = 0;

void do_print_debug(const char* file, int line, const char* func, const char* fmt, ...) {
    fflush(stdout);
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d: %s: ", base, line, func);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr); // TODO: output is getting lost
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

// While this should never fail given a valid handle,
// returns MX_KOID_INVALID on failure.

mx_koid_t get_koid(mx_handle_t handle) {
    mx_info_handle_basic_t info;
    mx_ssize_t size = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, sizeof(info.rec),
                                         &info, sizeof(info));
    if (size == sizeof(info))
        return info.rec.koid;
    // This shouldn't ever happen, so don't just ignore it.
    fprintf(stderr, "Eh? MX_INFO_HANDLE_BASIC failed\n");
    // OTOH we can't just fail, we have to be robust about reporting back
    // to the kernel that we handled the exception.
    // TODO: Provide ability to safely terminate at any point (e.g., for assert
    // failures and such).
    return MX_KOID_INVALID;
}

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <lib/console.h>
#include <lib/user_copy.h>

#include <lk/init.h>
#include <platform/debug.h>

#include <magenta/user_copy.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxDebugWriteSize = 256u;

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

int sys_debug_read(void* ptr, uint32_t len) {
    LTRACEF("ptr %p\n", ptr);

    if (!len)
        return 0;
    // TODO: remove this cast.
    auto uptr = reinterpret_cast<uint8_t*>(ptr);
    auto end = uptr + len;

    for (; uptr != end; ++uptr) {
        int c = getchar();
        if (c < 0)
            break;

        if (c == '\r')
            c = '\n';
        if (copy_to_user_u8(uptr, static_cast<uint8_t>(c)) != NO_ERROR)
            break;
    }
    // TODO: fix this cast, which can overflow.
    return static_cast<int>(reinterpret_cast<char*>(uptr) - reinterpret_cast<char*>(ptr));
}

int sys_debug_write(const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %d\n", ptr, len);

    if (len > kMaxDebugWriteSize)
        return ERR_TOO_BIG;

    char buf[kMaxDebugWriteSize];
    if (magenta_copy_from_user(ptr, buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    for (uint32_t i = 0; i < len; i++) {
        platform_dputc(buf[i]);
    }
    return len;
}

int sys_debug_send_command(const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %d\n", ptr, len);

    if (len > kMaxDebugWriteSize)
        return ERR_INVALID_ARGS;

    char buf[kMaxDebugWriteSize + 2];
    if (magenta_copy_from_user(ptr, buf, len) != NO_ERROR)
        return ERR_INVALID_ARGS;

    buf[len] = '\n';
    buf[len + 1] = 0;
    return console_run_script(buf);
}

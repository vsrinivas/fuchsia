// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

ssize_t cmdline_get(const char* cmdline, const char* key, char* value, size_t n) {
    if (!cmdline || !key) return -1;
    unsigned sz = strlen(key);
    const char* ptr = cmdline;
    for (;;) {
        if (!memcmp(ptr, key, sz)) {
            break;
        }
        ptr = strchr(ptr, ' ');
        if (ptr == NULL) {
            return -1;
        }
        ptr++;
    }
    ptr += sz;
    if (*ptr == '=') {
        ptr++;
    }
    size_t len = 0;
    while (n--) {
        if (isspace(*ptr)) break;
        *value++ = *ptr++;
        len++;
    }
    if (n) {
        *value = '\0';
    } else {
        *(value - 1) = '\0';
    }
    return len;
}

uint32_t cmdline_get_uint32(const char* cmdline, const char* key, uint32_t _default) {
    char val[11];
    ssize_t ret = cmdline_get(cmdline, key, val, sizeof(val));
    if (ret < 0) {
        return _default;
    }
    return atol(val);
}

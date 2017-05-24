// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/cmdline.h>

char __kernel_cmdline[CMDLINE_MAX];
unsigned __kernel_cmdline_size;
unsigned __kernel_cmdline_count;

// import into kernel commandline, converting invalid
// characters to '.', combining multiple spaces, and
// converting into a \0 separated, \0\0 terminated
// style environment string
void cmdline_append(const char* data) {
    unsigned i = __kernel_cmdline_size;
    unsigned max = CMDLINE_MAX - 2;

    bool found_equal = false;
    while (i < max) {
        unsigned c = *data++;
        if (c == 0) {
            if (found_equal) {              //last option was null delimited
                ++__kernel_cmdline_count;
            }
            break;
        }
        if (c == '=') {
            found_equal = true;
        }
        if ((c < ' ') || (c > 127)) {
            if ((c == '\n') || (c == '\r') || (c == '\t')) {
                c = ' ';
            } else {
                c = '.';
            }
        }
        if (c == ' ') {
            // spaces become \0's, but do not double up
            if ((i == 0) || (__kernel_cmdline[i-1] == 0)) {
                continue;
            } else {
                if (!found_equal && i < max) {
                    __kernel_cmdline[i++] = '=';
                }
                c = 0;
                found_equal = false;
                ++__kernel_cmdline_count;
            }
        }
        __kernel_cmdline[i++] = c;
    }
    if (!found_equal && i > 0 && __kernel_cmdline[i-1] != '\0' && i < max) {
        __kernel_cmdline[i++] = '=';
        ++__kernel_cmdline_count;
    }

    // ensure a double-\0 terminator
    __kernel_cmdline[i++] = 0;
    __kernel_cmdline[i] = 0;
    __kernel_cmdline_size = i;
}

const char* cmdline_get(const char* key) {
    if (!key) return __kernel_cmdline;
    unsigned sz = strlen(key);
    const char* ptr = __kernel_cmdline;
    for (;;) {
        if (!strncmp(ptr, key, sz)) {
            break;
        }
        ptr = strchr(ptr, 0) + 1;
        if (*ptr == 0) {
            return NULL;
        }
    }
    ptr += sz;
    if (*ptr == '=') {
        ptr++;
    }
    return ptr;
}

bool cmdline_get_bool(const char* key, bool _default) {
    const char* value = cmdline_get(key);
    if (value == NULL) {
        return _default;
    }
    if ((strcmp(value, "0") == 0) ||
        (strcmp(value, "false") == 0) ||
        (strcmp(value, "off") == 0)) {
        return false;
    }
    return true;
}

uint32_t cmdline_get_uint32(const char* key, uint32_t _default) {
    const char* value_str = cmdline_get(key);
    if (value_str == NULL || *value_str == '\0') {
        return _default;
    }

    char* end;
    long int value = strtol(value_str, &end, 0);
    if (*end != '\0') {
        return _default;
    }
    return value;
}

uint64_t cmdline_get_uint64(const char* key, uint64_t _default) {
    const char* value_str = cmdline_get(key);
    if (value_str == NULL || *value_str == '\0') {
        return _default;
    }

    char* end;
    long long value = strtoll(value_str, &end, 0);
    if (*end != '\0') {
        return _default;
    }
    return value;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

void* memset(void* _dst, int c, size_t n) {
    uint8_t* dst = _dst;
    while (n-- > 0) {
        *dst++ = c;
    }
    return _dst;
}

void* memcpy(void* _dst, const void* _src, size_t n) {
    uint8_t* dst = _dst;
    const uint8_t* src = _src;
    while (n-- > 0) {
        *dst++ = *src++;
    }
    return _dst;
}

int memcmp(const void* _a, const void* _b, size_t n) {
    const uint8_t* a = _a;
    const uint8_t* b = _b;
    while (n-- > 0) {
        int x = *a++ - *b++;
        if (x != 0) {
            return x;
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++)
        len++;
    return len;
}

size_t strnlen(const char* s, size_t max) {
    size_t len = 0;
    while (len < max && *s++)
        len++;
    return len;
}

char* strchr(const char* s, int c) {
    while (*s != c && *s++) ;
    if (*s != c) return 0;
    return (char*)s;
}

char* strcpy(char* dst, const char* src) {
    while (*src != 0) {
        *dst++ = *src++;
    }
    return dst;
}

char* strncpy(char* dst, const char* src, size_t len) {
    while (len-- > 0 && *src != 0) {
        *dst++ = *src++;
    }
    return dst;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

int strncmp(const char* s1, const char* s2, size_t len) {
    while (len-- > 0) {
        int diff = *s1 - *s2;
        if (diff != 0 || *s1 == '\0') {
            return diff;
        }
        s1++;
        s2++;
    }
    return 0;
}


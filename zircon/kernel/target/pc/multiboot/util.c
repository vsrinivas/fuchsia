// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>

void* memcpy(void* restrict dest, const void* restrict src, size_t len) {
    void* dest_end;
    void* src_end;
    __asm__ volatile("rep movsb" :
                     "=D"(dest_end), "=S"(src_end), "=c"(len) :
                     "0"(dest), "1"(src), "2"(len) :
                     "memory");
    return dest;
}

void* memmove(void* restrict dest, const void* restrict src, size_t len) {
    if ((uintptr_t)dest < (uintptr_t)src) {
        void* dest_end;
        void* src_end;
        __asm__ volatile("rep movsb" :
                         "=D"(dest_end), "=S"(src_end), "=c"(len) :
                         "0"(dest), "1"(src), "2"(len) :
                         "memory");
    } else {
        __asm__ volatile("std\n\t"
                         "rep movsb\n\t"
                         "cld" :
                         "=D"(dest), "=S"(src), "=c"(len) :
                         "0"((uint8_t*)dest + len - 1),
                         "1"((uint8_t*)src + len - 1),
                         "2"(len) :
                         "memory");
        dest = (void*)((uint8_t*)dest + 1);
    }
    return dest;
}

void* memset(void* dest, int val, size_t len) {
    void* dest_end;
    __asm__ volatile("rep stosb" :
                     "=D"(dest_end), "=c"(len) :
                     "0"(dest), "1"(len), "a"((unsigned char)val) :
                     "memory");
    return dest;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++ != '\0') {
        ++len;
    }
    return len;
}

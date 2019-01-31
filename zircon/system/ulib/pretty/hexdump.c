// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/hexdump.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void hexdump_ex(const void* ptr, size_t len, uint64_t disp_addr) {
    uintptr_t address = (uintptr_t)ptr;
    size_t count;

    for (count = 0; count < len; count += 16) {
        union {
            uint32_t buf[4];
            uint8_t cbuf[16];
        } u;
        size_t s = ROUNDUP(MIN(len - count, 16), 4);
        size_t i;

        printf(((disp_addr + len) > 0xFFFFFFFF)
                   ? "0x%016" PRIx64 ": "
                   : "0x%08" PRIx64 ": ",
               disp_addr + count);

        for (i = 0; i < s / 4; i++) {
            u.buf[i] = ((const uint32_t*)address)[i];
            printf("%08x ", u.buf[i]);
        }
        for (; i < 4; i++) {
            printf("         ");
        }
        printf("|");

        for (i = 0; i < 16; i++) {
            char c = u.cbuf[i];
            if (i < s && isprint(c)) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("|\n");
        address += 16;
    }
}

void hexdump8_ex(const void* ptr, size_t len, uint64_t disp_addr) {
    uintptr_t address = (uintptr_t)ptr;
    size_t count;
    size_t i;

    for (count = 0; count < len; count += 16) {
        printf(((disp_addr + len) > 0xFFFFFFFF)
                   ? "0x%016" PRIx64 ": "
                   : "0x%08" PRIx64 ": ",
               disp_addr + count);

        for (i = 0; i < MIN(len - count, 16); i++) {
            printf("%02hhx ", *(const uint8_t*)(address + i));
        }

        for (; i < 16; i++) {
            printf("   ");
        }

        printf("|");

        for (i = 0; i < MIN(len - count, 16); i++) {
            char c = ((const char*)address)[i];
            printf("%c", isprint(c) ? c : '.');
        }

        printf("\n");
        address += 16;
    }
}

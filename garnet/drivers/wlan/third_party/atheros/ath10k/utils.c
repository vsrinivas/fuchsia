/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "utils.h"

#include <stdio.h>

#include "hw.h"

void ethaddr_sprintf(char* str, const uint8_t* addr) {
    bool first = true;
    for (unsigned ndx = 0; ndx < ETH_ALEN; ndx++) {
        str += sprintf(str, "%s%02X", first ? "" : ":", *addr);
        addr++;
        first = false;
    }
}

bool is_zero_ether_addr(const uint8_t* addr) {
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        if (addr[i] != 0) {
            return false;
        }
    }
    return true;
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_id.h"

#include <stdio.h>
#include <string.h>

#include "eff_short_wordlist_1.h"

#define APPEND_WORD(NUM, SEP)         \
    word = dictionary[(NUM) % 1296];  \
    memcpy(dest, word, strlen(word)); \
    dest += strlen(word);             \
    *dest = SEP;                      \
    dest++;

void device_id(mac_addr addr, char out[DEVICE_ID_MAX]) {
    const char* word;
    char* dest = out;
    APPEND_WORD(addr.x[0] | ((addr.x[4] << 8) & 0xF00), '-');
    APPEND_WORD(addr.x[1] | ((addr.x[5] << 8) & 0xF00), '-');
    APPEND_WORD(addr.x[2] | ((addr.x[4] << 4) & 0xF00), '-');
    APPEND_WORD(addr.x[3] | ((addr.x[5] << 4) & 0xF00), 0);
}

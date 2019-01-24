// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include "device_id.h"
#include "eff_short_wordlist_1.h"

#define APPEND_WORD(NUM, SEP)                            \
    word = dictionary[(NUM) % DICEWARE_DICTIONARY_SIZE]; \
    memcpy(dest, word, strlen(word));                    \
    dest += strlen(word);                                \
    *dest = SEP;                                         \
    dest++;

void device_id_get(unsigned char mac[6], char out[DEVICE_ID_MAX]) {
    const char* word;
    char* dest = out;
    APPEND_WORD(mac[0] | ((mac[4] << 8) & 0xF00), '-');
    APPEND_WORD(mac[1] | ((mac[5] << 8) & 0xF00), '-');
    APPEND_WORD(mac[2] | ((mac[4] << 4) & 0xF00), '-');
    APPEND_WORD(mac[3] | ((mac[5] << 4) & 0xF00), 0);
}

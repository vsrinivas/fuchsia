// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_id.h"

#include <stdio.h>
#include <string.h>

#include "name_tokens.h"

// Copies a word from the wordlist starting at |dest| and then adds |sep| at the end.
// Returns a pointer to the character after the separator.
char* append_word(char* dest, uint16_t num, char sep) {
  const char* word = dictionary[num % TOKEN_DICTIONARY_SIZE];
  memcpy(dest, word, strlen(word));
  dest += strlen(word);
  *dest = sep;
  dest++;
  return dest;
}

void device_id_get_words(mac_addr addr, char out[DEVICE_ID_MAX]) {
  char* dest = out;
  dest = append_word(dest, (uint16_t)(addr.x[0] | ((addr.x[4] << 8) & 0xF00)), '-');
  dest = append_word(dest, (uint16_t)(addr.x[1] | ((addr.x[5] << 8) & 0xF00)), '-');
  dest = append_word(dest, (uint16_t)(addr.x[2] | ((addr.x[4] << 4) & 0xF00)), '-');
  dest = append_word(dest, (uint16_t)(addr.x[3] | ((addr.x[5] << 4) & 0xF00)), 0);
}

const char hex_chars[17] = "0123456789abcdef";

// Copies 4 hex characters of hex value of the bits of |num|.
// Then writes |sep| to the character after.
// Returns a pointer to the character after the separator.
char* append_hex(char* dest, uint16_t num, char sep) {
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t left = num >> ((3 - i) * 4);
    *dest = hex_chars[left & 0x0F];
    dest++;
  }
  *dest = sep;
  dest++;
  return dest;
}

#define PREFIX_LEN 9
const char mac_prefix[PREFIX_LEN] = "fuchsia-";

void device_id_get_mac(mac_addr addr, char out[DEVICE_ID_MAX]) {
  char* dest = out;
  // Prepended with mac_prefix
  for (uint8_t i = 0; i < PREFIX_LEN; i++) {
    dest[i] = mac_prefix[i];
  }
  dest = dest + PREFIX_LEN - 1;
  dest = append_hex(dest, (uint16_t)((addr.x[0] << 8) | addr.x[1]), '-');
  dest = append_hex(dest, (uint16_t)((addr.x[2] << 8) | addr.x[3]), '-');
  dest = append_hex(dest, (uint16_t)((addr.x[4] << 8) | addr.x[5]), 0);
}

void device_id(mac_addr addr, char out[DEVICE_ID_MAX], uint32_t generation) {
  if (generation == 1) {
    device_id_get_mac(addr, out);
  } else {  // Style 0
    device_id_get_words(addr, out);
  }
}

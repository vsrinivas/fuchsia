// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_SRC_CMDLINE_H_
#define ZIRCON_BOOTLOADER_SRC_CMDLINE_H_

#include <stddef.h>
#include <stdint.h>

// append a commandline string to the commandline
void cmdline_append(const char* str, size_t len);

// add a commandline item to the commandline
// (replaces items with the same name)
void cmdline_set(const char* key, const char* val);

// look up an item in the commandline
const char* cmdline_get(const char* key, const char* _default);
uint32_t cmdline_get_uint32(const char* key, uint32_t _default);

// obtain the entire commandline as a string
size_t cmdline_to_string(char* ptr, size_t max);

#endif  // ZIRCON_BOOTLOADER_SRC_CMDLINE_H_

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define CMDLINE_MAX 4096

// initialize commandline from str
// if string is not null terminated, it will be limited to CMDLINE_MAX
void cmdline_append(const char* str);

// look for "name" or "name=..." in the commandline
// returns NULL if not found, asciiz string if found
const char* cmdline_get(const char* key);

// return _default if key not found
// return false if key's value is "0", "false", "off"
// return true otherwise
bool cmdline_get_bool(const char* key, bool _default);

// return _default if key not found or invalid
// return they key's integer value otherwise
uint32_t cmdline_get_uint32(const char* key, uint32_t _default);

// return _default if key not found or invalid
// return they key's integer value otherwise
uint64_t cmdline_get_uint64(const char* key, uint64_t _default);

__END_CDECLS

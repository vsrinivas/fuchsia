// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPT_C_GPT_H_
#define GPT_C_GPT_H_

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/hw/gpt.h>
#include <zircon/types.h>

__BEGIN_CDECLS
typedef gpt_entry_t gpt_partition_t;

// Helpers for translating the |name| field of "gpt_partition_t".
// Assumes UTF-16LE.
// Assumes all code points are less than or equal to U+007F, and
// discards any upper bits, forcing all inputs to be in this
// range.
//
// |len| refers to the length of the input string, in chars.
void cstring_to_utf16(uint16_t* dst, const char* src, size_t len);
// |len| refers to the length of the input string, in 16-bit pairs.
char* utf16_to_cstring(char* dst, const uint16_t* src, size_t len);

// converts GUID to a string
void uint8_to_guid_string(char* dst, const uint8_t* src);

// determines whether guid is system guid
bool gpt_is_sys_guid(uint8_t* guid, ssize_t len);

// determines whether guid is data guid
bool gpt_is_data_guid(uint8_t* guid, ssize_t len);

// determines whether guid is install guid
bool gpt_is_install_guid(uint8_t* guid, ssize_t len);

// determines whether guid is efi guid
bool gpt_is_efi_guid(uint8_t* guid, ssize_t len);

// determines whether guid is factory guid
bool gpt_is_factory_guid(uint8_t* guid, ssize_t len);

void gpt_set_debug_output_enabled(bool enabled);

// Sort an array of gpt_partition_t pointers in-place based on the values of
// gpt_partition_t->first.
void gpt_sort_partitions(gpt_partition_t** partitions, size_t count);

__END_CDECLS
#endif  // GPT_C_GPT_H_

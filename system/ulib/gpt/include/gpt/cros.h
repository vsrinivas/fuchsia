// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

// GUID for a ChromeOS kernel partition
#define GUID_CROS_KERNEL { \
    0x5d, 0x2a, 0x3a, 0xfe, \
    0x32, 0x4f, \
    0xa7, 0x41, \
    0xb7, 0x25, 0xac, 0xcc, 0x32, 0x85, 0xa3, 0x09 \
}

// Returns true if |guid| matches the ChromeOS kernel GUID.
bool gpt_cros_is_kernel_guid(const uint8_t* guid, size_t len);

// Gets/sets the successful flag for a CrOS KERNEL partition.
bool gpt_cros_attr_get_successful(uint64_t flags);
void gpt_cros_attr_set_successful(uint64_t* flags, bool successful);

// Gets/sets the tries remaining field for a CrOS KERNEL partition.
// tries must be in the range [0, 16).  If it is out of range, -1
// is returned from set.  Otherwise returns 0.
uint8_t gpt_cros_attr_get_tries(uint64_t flags);
int gpt_cros_attr_set_tries(uint64_t* flags, uint8_t tries);

// Gets/sets the priority field for a CrOS KERNEL partition.
// priority must be in the range [0, 16).  If it is out of range, -1
// is returned from set.  Otherwise returns 0.
uint8_t gpt_cros_attr_get_priority(uint64_t flags);
int gpt_cros_attr_set_priority(uint64_t* flags, uint8_t priority);

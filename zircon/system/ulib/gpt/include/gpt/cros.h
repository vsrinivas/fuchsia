// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS

// GUID for a ChromeOS kernel partition
#define GUID_CROS_KERNEL_STRING "FE3A2A5D-4F32-41A7-B725-ACCC3285A309"
#define GUID_CROS_KERNEL_VALUE { \
    0x5d, 0x2a, 0x3a, 0xfe, \
    0x32, 0x4f, \
    0xa7, 0x41, \
    0xb7, 0x25, 0xac, 0xcc, 0x32, 0x85, 0xa3, 0x09 \
}

#define GUID_CROS_ROOT_STRING "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC"
#define GUID_CROS_ROOT_VALUE { \
    0x02, 0xe2, 0xb8, 0x3c, \
    0x7e, 0x3b, \
    0xdd, 0x47, \
    0x8a, 0x3c, 0x7f, 0xf2, 0xa1, 0x3c, 0xfc, 0xec \
}

#define GUID_GEN_DATA_STRING "EBD0A0A2-B9E5-4433-87C0-68B8B72699C7"
#define GUID_GEN_DATA_VALUE { \
    0xa2, 0xa0, 0xd0, 0xeb, \
    0xe5, 0xb9, \
    0x33, 0x44, \
    0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 \
}

#define GUID_CROS_STATE_STRING GUID_GEN_DATA_STRING
#define GUID_CROS_STATE_VALUE GUID_GEN_DATA_VALUE

#define GUID_CROS_FIRMWARE_STRING "CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3"
#define GUID_CROS_FIRMWARE_VALUE { \
    0x8e, 0xe8, 0xb6, 0xca, \
    0xf3, 0xab, \
    0x02, 0x41, \
    0xa0, 0x7a, 0xd4, 0xbb, 0x9b, 0xe3, 0xc1, 0xd3 \
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

__END_CDECLS

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/cros.h>

#include <gpt/gpt.h>
#include <string.h>

#define PRIORITY_SHIFT 48
#define PRIORITY_MASK (15ull << PRIORITY_SHIFT)

#define TRIES_SHIFT 52
#define TRIES_MASK (15ull << TRIES_SHIFT)

#define SUCCESSFUL_SHIFT 56
#define SUCCESSFUL_MASK (1ull << SUCCESSFUL_SHIFT)

bool gpt_cros_is_kernel_guid(const uint8_t* guid, size_t len) {
    static const uint8_t kernel_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL;
    return len == GPT_GUID_LEN && !memcmp(guid, kernel_guid, GPT_GUID_LEN);
}

bool gpt_cros_attr_get_successful(uint64_t flags) {
    return flags & SUCCESSFUL_MASK;
}

void gpt_cros_attr_set_successful(uint64_t* flags, bool successful) {
    *flags = (*flags & ~SUCCESSFUL_MASK) | ((uint64_t)successful << SUCCESSFUL_SHIFT);
}

uint8_t gpt_cros_attr_get_tries(uint64_t flags) {
    return (flags & TRIES_MASK) >> TRIES_SHIFT;
}

int gpt_cros_attr_set_tries(uint64_t* flags, uint8_t tries) {
    if (tries >= 16) {
        return -1;
    }

    *flags = (*flags & ~TRIES_MASK) | ((uint64_t)tries << TRIES_SHIFT);
    return 0;
}

uint8_t gpt_cros_attr_get_priority(uint64_t flags) {
    return (flags & PRIORITY_MASK) >> PRIORITY_SHIFT;
}

int gpt_cros_attr_set_priority(uint64_t* flags, uint8_t priority) {
    if (priority >= 16) {
        return -1;
    }

    *flags = (*flags & ~PRIORITY_MASK) | ((uint64_t)priority << PRIORITY_SHIFT);
    return 0;
}

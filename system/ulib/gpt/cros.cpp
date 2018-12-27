// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/cros.h>

#include <gpt/c/gpt.h>
#include <string.h>

namespace gpt {

namespace {
constexpr uint8_t kPriorityShift = 48;
constexpr uint64_t kPriorityMask = 15ull << kPriorityShift;

constexpr uint8_t kTriesShift = 52;
constexpr uint64_t kTriesMask = 15ull << kTriesShift;

constexpr uint8_t kSuccessfulShift = 56;
constexpr uint64_t kSuccessfulMask = 1ull << kSuccessfulShift;
} // namespace

__BEGIN_CDECLS
bool gpt_cros_is_kernel_guid(const uint8_t* guid, size_t len) {
    static const uint8_t kernel_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
    return len == GPT_GUID_LEN && !memcmp(guid, kernel_guid, GPT_GUID_LEN);
}

bool gpt_cros_attr_get_successful(uint64_t flags) {
    return flags & kSuccessfulMask;
}

void gpt_cros_attr_set_successful(uint64_t* flags, bool successful) {
    *flags = (*flags & ~kSuccessfulMask) | ((uint64_t)successful << kSuccessfulShift);
}

uint8_t gpt_cros_attr_get_tries(uint64_t flags) {
    return (flags & kTriesMask) >> kTriesShift;
}

int gpt_cros_attr_set_tries(uint64_t* flags, uint8_t tries) {
    if (tries >= 16) {
        return -1;
    }

    *flags = (*flags & ~kTriesMask) | ((uint64_t)tries << kTriesShift);
    return 0;
}

uint8_t gpt_cros_attr_get_priority(uint64_t flags) {
    return (flags & kPriorityMask) >> kPriorityShift;
}

int gpt_cros_attr_set_priority(uint64_t* flags, uint8_t priority) {
    if (priority >= 16) {
        return -1;
    }

    *flags = (*flags & ~kPriorityMask) | ((uint64_t)priority << kPriorityShift);
    return 0;
}

__END_CDECLS
} // namespace gpt

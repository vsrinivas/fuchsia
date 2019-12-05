// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FS_JOURNAL_FORMAT_ASSERTIONS_H_
#define ZIRCON_SYSTEM_ULIB_FS_JOURNAL_FORMAT_ASSERTIONS_H_

#include <cstddef>

#include <fs/journal/format.h>

// This file tests the on-disk structures of journal.

namespace fs {

// clang-format off

#define PADDING_LENGTH(type, prev, next)                                                 \
    (offsetof(type, next) - (offsetof(type, prev) + sizeof(type{}.prev)))

// Ensure that the members don't change their offsets within the structure
static_assert(offsetof(fs::JournalInfo, magic) ==                0x0);
static_assert(offsetof(fs::JournalInfo, start_block) ==          0x08);
static_assert(offsetof(fs::JournalInfo, reserved) ==             0x10);
static_assert(offsetof(fs::JournalInfo, timestamp) ==            0x18);
static_assert(offsetof(fs::JournalInfo, checksum) ==             0x20);

// Ensure that the padding between two members doesn't change
static_assert(PADDING_LENGTH(fs::JournalInfo, magic,            start_block) ==           0);
static_assert(PADDING_LENGTH(fs::JournalInfo, start_block,      reserved) ==            0);
static_assert(PADDING_LENGTH(fs::JournalInfo, reserved,         timestamp) ==             0);
static_assert(PADDING_LENGTH(fs::JournalInfo, timestamp,        checksum) ==              0);

// Ensure that the padding at the end of structure doesn't change
static_assert(sizeof(fs::JournalInfo) ==
              offsetof(fs::JournalInfo, checksum) +
              sizeof(fs::JournalInfo{}.checksum) + 4);

// Ensure that the members don't change their offsets within the structure.
static_assert(offsetof(fs::JournalPrefix, magic) ==                0x0);
static_assert(offsetof(fs::JournalPrefix, sequence_number) ==      0x8);
static_assert(offsetof(fs::JournalPrefix, flags) ==                0x10);

// Ensure that the padding between members doesn't change.
static_assert(PADDING_LENGTH(fs::JournalPrefix, magic,           sequence_number) ==  0);
static_assert(PADDING_LENGTH(fs::JournalPrefix, sequence_number, flags) ==            0);

// Ensure that the members don't change their offsets within the structure.
static_assert(offsetof(fs::JournalHeaderBlock, prefix) ==          0x0);
static_assert(offsetof(fs::JournalHeaderBlock, payload_blocks) ==  0x20);
static_assert(offsetof(fs::JournalHeaderBlock, target_blocks) ==   0x28);
static_assert(offsetof(fs::JournalHeaderBlock, target_flags)  ==   0x1560);

// Ensure that the padding between members doesn't change.
static_assert(PADDING_LENGTH(fs::JournalHeaderBlock, prefix,     payload_blocks)   ==  0);
static_assert(PADDING_LENGTH(fs::JournalHeaderBlock, payload_blocks, target_blocks) == 0);
static_assert(PADDING_LENGTH(fs::JournalHeaderBlock, target_blocks, target_flags) ==   0);

// Ensure that the members don't change their offsets within the structure.
static_assert(offsetof(fs::JournalCommitBlock, prefix) ==   0x0);
static_assert(offsetof(fs::JournalCommitBlock, checksum) == 0x20);

// Ensure that the padding between members doesn't change.
static_assert(PADDING_LENGTH(fs::JournalCommitBlock, prefix, checksum) == 0);

// clang-format on

}  // namespace fs

#endif  // ZIRCON_SYSTEM_ULIB_FS_JOURNAL_FORMAT_ASSERTIONS_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector_journal_entries.h"

#include <lib/disk-inspector/common-types.h>

#include <array>

namespace fs {
namespace {

// Number of struct elements within |JournalPrefix|.
constexpr size_t kPrefixElements = 4;

std::unique_ptr<disk_inspector::DiskObject> ParsePrefix(const fs::JournalPrefix* prefix,
                                                        size_t index) {
  switch (index) {
    case 0:
      return std::make_unique<disk_inspector::DiskObjectUint64>("magic", &(prefix->magic));
    case 1:
      return std::make_unique<disk_inspector::DiskObjectUint64>("sequence number",
                                                                &(prefix->sequence_number));
    case 2:
      return std::make_unique<disk_inspector::DiskObjectUint64>("flags", &(prefix->flags));
    case 3:
      return std::make_unique<disk_inspector::DiskObjectUint64>("reserved", &(prefix->reserved));
  }
  return nullptr;
}

}  // namespace

JournalBlock::JournalBlock(uint32_t index, fs::JournalInfo info,
                           std::array<uint8_t, kJournalBlockSize> block)
    : index_(index), journal_info_(std::move(info)), block_(std::move(block)) {
  auto prefix = reinterpret_cast<const fs::JournalPrefix*>(block_.data());
  if (prefix->magic == fs::kJournalEntryMagic) {
    object_type_ = prefix->ObjectType();
  } else {
    // Treat non-journal objects as "blocks".
    //
    // They will not be parsed further, but they'll be identified as a
    // non-journal object.
    object_type_ = fs::JournalObjectType::kUnknown;
    name_ = fbl::StringPrintf("Journal[%d]: Block", index_);
    return;
  }

  switch (object_type_) {
    case fs::JournalObjectType::kHeader: {
      auto header = reinterpret_cast<const fs::JournalHeaderBlock*>(block_.data());
      // Counting the number of fields within the struct:
      //
      // JournalHeaderBlock {
      //   Prefix             (kPrefixElements)
      //   payload_blocks     (1)
      //   target_blocks[...] (header->payload_blocks)
      // }
      num_elements_ = static_cast<uint32_t>(kPrefixElements + 1 + header->payload_blocks);
      name_ = fbl::StringPrintf("Journal[%d]: Header", index_);
      break;
    }
    case fs::JournalObjectType::kCommit:
      num_elements_ = kPrefixElements;
      name_ = fbl::StringPrintf("Journal[%d]: Commit", index_);
      break;
    case fs::JournalObjectType::kRevocation:
      num_elements_ = kPrefixElements;
      name_ = fbl::StringPrintf("Journal[%d]: Revocation", index_);
      break;
    default:
      name_ = fbl::StringPrintf("Journal[%d]: Unknown", index_);
  }
}

uint32_t JournalBlock::GetNumElements() const { return num_elements_; }

void JournalBlock::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  // JournalBlocks themselves don't have values that we can meaningfully print,
  // so instead just return a meaningless fixed value (returning a null buffer
  // would cause the inspector framework to crash).
  static uint32_t sentinel = 0;
  *out_buffer = &sentinel;
  *out_buffer_size = sizeof(sentinel);
}

std::unique_ptr<disk_inspector::DiskObject> JournalBlock::GetElementAt(uint32_t index) const {
  auto prefix = reinterpret_cast<const fs::JournalPrefix*>(block_.data());
  switch (object_type_) {
    case fs::JournalObjectType::kHeader: {
      if (index < kPrefixElements) {
        return ParsePrefix(prefix, index);
      }
      auto header = reinterpret_cast<const fs::JournalHeaderBlock*>(block_.data());
      if (index == kPrefixElements) {
        return std::make_unique<disk_inspector::DiskObjectUint64>("payload blocks",
                                                                  &(header->payload_blocks));
      }

      constexpr size_t kPayloadIndex = kPrefixElements + 1;
      if (index - kPayloadIndex < header->payload_blocks) {
        return std::make_unique<disk_inspector::DiskObjectUint64>(
            "target block", &(header->target_blocks[index - kPayloadIndex]));
      }
      return nullptr;
    }
    case fs::JournalObjectType::kCommit:
      return ParsePrefix(prefix, index);
    case fs::JournalObjectType::kRevocation:
      return ParsePrefix(prefix, index);
    default:
      return nullptr;
  }
  return nullptr;
}

void JournalEntries::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> JournalEntries::GetElementAt(uint32_t index) const {
  if (index < length_) {
    std::array<uint8_t, kJournalBlockSize> data;
    zx_status_t status =
        inspectable_->ReadBlock(static_cast<blk_t>(start_block_ + index), data.data());
    if (status != ZX_OK) {
      return nullptr;
    }

    return std::make_unique<JournalBlock>(index, journal_info_, std::move(data));
  }
  return nullptr;
}

}  // namespace fs

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/abr-client-vboot.h"

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <memory>

#include <gpt/cros.h>
#include <gpt/gpt.h>

#include "lib/abr/abr.h"
#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/pave-logging.h"

namespace abr {

using uuid::Uuid;

namespace {
// Converts a vboot-style partition state to a libabr-style slot state.
AbrSlotData GetSlotState(const gpt_partition_t* partition) {
  uint8_t priority = gpt_cros_attr_get_priority(partition->flags);
  uint8_t tries_remaining = gpt_cros_attr_get_tries(partition->flags);
  uint8_t successful_boot = gpt_cros_attr_get_successful(partition->flags);
  return AbrSlotData{
      .priority = priority,
      .tries_remaining = tries_remaining,
      .successful_boot = successful_boot,
  };
}

void SetSlotState(gpt_partition_t* partition, const AbrSlotData* data) {
  gpt_cros_attr_set_priority(&partition->flags, data->priority);
  gpt_cros_attr_set_tries(&partition->flags, data->tries_remaining);
  gpt_cros_attr_set_successful(&partition->flags, data->successful_boot);
}

// Returns 0 for slot 'a', 1 for slot 'b', and -1 for all other partitions.
zx::result<AbrSlotIndex> GetSlotIndexForPartition(gpt_partition_t* partition) {
  char name_buf[GPT_NAME_LEN / 2];
  constexpr int kZirconLength = 6;  // strlen("zircon")
  utf16_to_cstring(name_buf, reinterpret_cast<uint16_t*>(partition->name), GPT_NAME_LEN / 2);

  if (strcasestr(name_buf, "zircon") != name_buf ||
      (name_buf[kZirconLength] != '-' && name_buf[kZirconLength] != '_')) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  switch (name_buf[kZirconLength + 1]) {
    case 'a':
    case 'A':
      return zx::ok(kAbrSlotIndexA);
    case 'b':
    case 'B':
      return zx::ok(kAbrSlotIndexB);
    case 'r':
    case 'R':
      return zx::ok(kAbrSlotIndexR);
    default:
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}
}  // namespace

zx::result<std::unique_ptr<abr::VbootClient>> VbootClient::Create(
    std::unique_ptr<paver::CrosDevicePartitioner> gpt) {
  return zx::ok(std::make_unique<VbootClient>(std::move(gpt)));
}

zx::result<> VbootClient::ReadCustom(AbrSlotData* a, AbrSlotData* b, uint8_t* one_shot_recovery) {
  gpt::GptDevice* gpt = gpt_->GetGpt();
  bool seen_a = false;
  bool seen_b = false;
  for (uint64_t i = 0; i < gpt->EntryCount(); i++) {
    auto part = gpt->GetPartition(static_cast<uint32_t>(i));
    if (part.is_error()) {
      break;
    }

    auto slot_index = GetSlotIndexForPartition(*part);
    if (slot_index.is_error() || *slot_index == kAbrSlotIndexR) {
      continue;
    }

    if (*slot_index == kAbrSlotIndexA) {
      *a = GetSlotState(*part);
      seen_a = true;
    } else if (*slot_index == kAbrSlotIndexB) {
      *b = GetSlotState(*part);
      seen_b = true;
    }
  }

  if (!seen_a || !seen_b) {
    ERROR("Device is missing one or more A/B/R partitions!");
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  *one_shot_recovery = 0;

  return zx::ok();
}

zx::result<> VbootClient::WriteCustom(const AbrSlotData* a, const AbrSlotData* b,
                                      uint8_t one_shot_recovery) {
  gpt::GptDevice* gpt = gpt_->GetGpt();
  uint8_t max_prio = std::max(a->priority, b->priority);
  bool seen_a = false;
  bool seen_b = false;
  for (uint64_t i = 0; i < gpt->EntryCount(); i++) {
    auto result = gpt->GetPartition(static_cast<uint32_t>(i));
    if (result.is_error()) {
      break;
    }

    auto part = *result;
    auto slot_index = GetSlotIndexForPartition(part);
    if (slot_index.is_error() || *slot_index == kAbrSlotIndexR) {
      if (Uuid(part->guid) == Uuid(GUID_CROS_KERNEL_VALUE) &&
          gpt_cros_attr_get_priority(part->flags) >= max_prio) {
        // Make sure all other partitions have a lower priority than the one we're trying to boot
        // from.
        gpt_cros_attr_set_priority(&part->flags, static_cast<uint8_t>(max_prio - 1));
      }

      // Make sure the recovery slot is always marked as successful.
      if (slot_index.value_or(kAbrSlotIndexA) == kAbrSlotIndexR) {
        gpt_cros_attr_set_successful(&part->flags, true);
      }
      continue;
    }

    if (*slot_index == kAbrSlotIndexA) {
      seen_a = true;
    } else if (*slot_index == kAbrSlotIndexB) {
      seen_b = true;
    }
    const AbrSlotData* cur = (*slot_index == kAbrSlotIndexA ? a : b);
    SetSlotState(part, cur);
  }

  if (!seen_a || !seen_b) {
    ERROR("Device is missing one or more A/B/R partitions!");
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  gpt->Sync();

  return zx::ok();
}

}  // namespace abr

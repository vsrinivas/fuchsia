// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

namespace abr {

// Magic for the A/B struct when serialized.
constexpr char kMagic[] = "\0AB0";
constexpr size_t kMagicLen = 4;

// Versioning for the on-disk A/B metadata.
constexpr size_t kMajorVersion = 2;
constexpr size_t kMinorVersion = 0;

// Maximum values for slot data.
constexpr uint8_t kMaxPriority = 15;
constexpr uint8_t kMaxTriesRemaining = 7;

// Struct used for recording per-slot metadata.
struct SlotData {
  // Slot priority. Valid values range from 0 to kMaxPriority,
  // both inclusive with 1 being the lowest and kMaxPriority
  // being the highest. The special value 0 is used to indicate the
  // slot is unbootable.
  uint8_t priority;

  // Number of remaining attempts to boot this slot ranging from 0
  // to kMaxTriesRemaining.
  uint8_t tries_remaining;

  // Non-zero if this slot has booted successfully, 0 otherwise.
  uint8_t successful_boot;

  // Reserved for future use.
  uint8_t reserved[1];
} __PACKED;

// Struct used for recording A/B/R metadata.
//
// When serialized, data is stored in network byte-order.
struct Data {
  // Magic number used for identification - see kMagic.
  uint8_t magic[kMagicLen];

  // Version of on-disk struct - see k{Major,Minor}Version.
  uint8_t version_major;
  uint8_t version_minor;

  // Padding to ensure |slots| field start eight bytes in.
  uint8_t reserved1[2];

  // A/B per-slot metadata. Recovery boot does not have its own
  // data and will be used if both A/B slots are not bootable
  SlotData slots[2];

  // Oneshot force recovery boot. Currently unused.
  uint8_t oneshot_recovery_boot;

  // Reserved for future use.
  uint8_t reserved2[11];

  // CRC32 of all 28 bytes preceding this field.
  uint32_t crc32;
} __PACKED;

static_assert(sizeof(Data) == 32);

}  // namespace abr

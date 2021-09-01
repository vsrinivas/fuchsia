// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_ACPI_UTIL_H_
#define SRC_DEVICES_LIB_ACPI_UTIL_H_

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/zx/status.h>
#include <stdint.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <exception>
#include <vector>

namespace acpi {

// We can't use //src/lib/uuid because it introduces a dependency on fxl.
static constexpr size_t kUuidBytes = 16;
struct Uuid {
  uint8_t bytes[kUuidBytes];
  // GUIDs are specified in mixed-endian, to avoid manual errors use this function.
  // Example usage: Uuid::Create(0x00112233, 0x4455, 0x6677, 0x8899, 0xAABBCCDDEEFF)
  static constexpr Uuid Create(const uint32_t group0, const uint16_t group1, const uint16_t group2,
                               const uint16_t group3, const uint64_t group4) {
    return {.bytes = {
                /* group0: 4 bytes, little-endian. */
                static_cast<uint8_t>((group0 >> 0) & 0xFF),
                static_cast<uint8_t>((group0 >> 8) & 0xFF),
                static_cast<uint8_t>((group0 >> 16) & 0xFF),
                static_cast<uint8_t>((group0 >> 24) & 0xFF),
                /* group1: 2 bytes, little-endian. */
                static_cast<uint8_t>((group1 >> 0) & 0xFF),
                static_cast<uint8_t>((group1 >> 8) & 0xFF),
                /* group2: 2 bytes, little-endian. */
                static_cast<uint8_t>((group2 >> 0) & 0xFF),
                static_cast<uint8_t>((group2 >> 8) & 0xFF),
                /* group3: 2 bytes, big-endian. */
                static_cast<uint8_t>((group3 >> 8) & 0xFF),
                static_cast<uint8_t>((group3 >> 0) & 0xFF),
                /* group4: 6 bytes, big-endian. */
                static_cast<uint8_t>((group4 >> 40) & 0xFF),
                static_cast<uint8_t>((group4 >> 32) & 0xFF),
                static_cast<uint8_t>((group4 >> 24) & 0xFF),
                static_cast<uint8_t>((group4 >> 16) & 0xFF),
                static_cast<uint8_t>((group4 >> 8) & 0xFF),
                static_cast<uint8_t>((group4 >> 0) & 0xFF),
            }};
  }

  bool operator==(const Uuid& b) const { return memcmp(bytes, b.bytes, kUuidBytes) == 0; }
};

}  // namespace acpi

namespace std {
static_assert(sizeof(size_t) <= acpi::kUuidBytes,
              "hash function assumes that size_t is smaller than a UUID");
template <>
struct hash<acpi::Uuid> {
  size_t operator()(const acpi::Uuid& uuid) const noexcept {
    size_t ret;
    memcpy(&ret, uuid.bytes, sizeof(size_t));
    return ret;
  }
};
}  // namespace std

#endif  // SRC_DEVICES_LIB_ACPI_UTIL_H_

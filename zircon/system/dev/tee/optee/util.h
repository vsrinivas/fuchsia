// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_TEE_OPTEE_UTIL_H_
#define ZIRCON_SYSTEM_DEV_TEE_OPTEE_UTIL_H_

#include <fuchsia/hardware/tee/llcpp/fidl.h>
#include <inttypes.h>
#include <stddef.h>

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;
namespace fuchsia_hardware_tee = ::llcpp::fuchsia::hardware::tee;

// Uuid
//
// Helper class for converting between the various representations of UUIDs. It is intended to
// remain consistent with the RFC 4122 definition of UUIDs. The UUID is 128 bits made up of 32
// bit time low, 16 bit time mid, 16 bit time high and 64 bit clock sequence and node fields. RFC
// 4122 states that when encoding a UUID as a sequence of bytes, each field will be encoded in
// network byte order. This class stores the data as a sequence of bytes.
struct Uuid final {
 public:
  explicit Uuid(const fuchsia_tee::Uuid& zx_uuid);

  void ToUint64Pair(uint64_t* out_hi, uint64_t* out_low) const;

 private:
  static constexpr size_t kUuidSize = 16;
  uint8_t data_[kUuidSize];
};

static_assert(sizeof(Uuid) == 16, "Uuid must remain exactly 16 bytes");

}  // namespace optee

#endif  // ZIRCON_SYSTEM_DEV_TEE_OPTEE_UTIL_H_

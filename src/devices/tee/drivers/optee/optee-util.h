// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_

#include <fuchsia/hardware/tee/llcpp/fidl.h>

#include <cinttypes>
#include <cstddef>
#include <string_view>

#include <ddk/debug.h>

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;
namespace fuchsia_hardware_tee = ::llcpp::fuchsia::hardware::tee;

constexpr std::string_view kDeviceName = "optee";

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

#define LOG(severity, fmt, ...) \
  zxlogf(severity, "[%s::%s] " fmt "\n", optee::kDeviceName.data(), __FUNCTION__, ##__VA_ARGS__);

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_

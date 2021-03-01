// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_

#include <fuchsia/hardware/tee/c/banjo.h>
#include <fuchsia/tee/llcpp/fidl.h>

#include <array>
#include <cinttypes>
#include <cstddef>

#include <ddk/debug.h>
#include <fbl/string_buffer.h>

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;

constexpr std::string_view kDeviceName = "optee";

// Uuid
//
// Helper class for converting between the various representations of UUIDs. It is intended to
// remain consistent with the RFC 4122 definition of UUIDs. The UUID is 128 bits made up of 32
// bit time low, 16 bit time mid, 16 bit time high and 64 bit clock sequence and node fields. RFC
// 4122 states that when encoding a UUID as a sequence of bytes, each field will be encoded in
// network byte order.
class Uuid final {
 public:
  // The Octet format is used when encoding a UUID as a sequence of bytes in network byte order.
  using Octets = std::array<uint8_t, 16>;

  // RFC 4122 specification dictates a UUID is of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx,
  // which is 36 characters.
  static constexpr size_t kUuidStringLength = 36;

  Uuid() = default;
  Uuid(uint32_t time_low, uint16_t time_mid, uint16_t time_hi_and_version,
       const std::array<uint8_t, 8>& clock_seq_and_node);
  explicit Uuid(const fuchsia_tee::wire::Uuid& zx_uuid);
  explicit Uuid(const uuid_t& uuid);
  explicit Uuid(const Octets& uuid);

  uint32_t time_low() const { return time_low_; }
  uint16_t time_mid() const { return time_mid_; }
  uint16_t time_hi_and_version() const { return time_hi_and_version_; }
  const std::array<uint8_t, 8>& clock_seq_and_node() const { return clock_seq_and_node_; }

  Octets ToOctets() const;

  fbl::StringBuffer<kUuidStringLength> ToString() const;

 private:
  template <typename T>
  void UuidInternal(const T&);

  uint32_t time_low_;
  uint16_t time_mid_;
  uint16_t time_hi_and_version_;
  std::array<uint8_t, 8> clock_seq_and_node_;
};

}  // namespace optee

#define LOG(severity, fmt, ...) \
  zxlogf(severity, "[%s::%s] " fmt "", optee::kDeviceName.data(), __FUNCTION__, ##__VA_ARGS__);

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_UTIL_H_

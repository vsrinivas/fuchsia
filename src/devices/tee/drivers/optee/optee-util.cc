// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-util.h"

#include <string.h>
#include <zircon/assert.h>

namespace optee {

Uuid::Uuid(uint32_t time_low, uint16_t time_mid, uint16_t time_hi_and_version,
           const std::array<uint8_t, 8>& clock_seq_and_node)
    : time_low_(time_low),
      time_mid_(time_mid),
      time_hi_and_version_(time_hi_and_version),
      clock_seq_and_node_(clock_seq_and_node) {}

Uuid::Uuid(const fuchsia_tee::wire::Uuid& uuid) { UuidInternal(uuid); }

Uuid::Uuid(const uuid_t& uuid) { UuidInternal(uuid); }

Uuid::Uuid(const Octets& uuid) {
  time_low_ = (static_cast<uint32_t>(uuid[0]) << 24) | (static_cast<uint32_t>(uuid[1]) << 16) |
              (static_cast<uint32_t>(uuid[2]) << 8) | static_cast<uint32_t>(uuid[3]);
  time_mid_ = (static_cast<uint16_t>(uuid[4]) << 8) | static_cast<uint16_t>(uuid[5]);
  time_hi_and_version_ = (static_cast<uint16_t>(uuid[6]) << 8) | static_cast<uint16_t>(uuid[7]);
  ::memcpy(clock_seq_and_node_.data(), uuid.data() + 8, uuid.size() - 8);
}

template <typename T>
void Uuid::UuidInternal(const T& uuid) {
  static_assert(std::is_same_v<T, fuchsia_tee::wire::Uuid> || std::is_same_v<T, uuid_t>);
  time_low_ = uuid.time_low;
  time_mid_ = uuid.time_mid;
  time_hi_and_version_ = uuid.time_hi_and_version;
  ::memcpy(clock_seq_and_node_.data(), &uuid.clock_seq_and_node, clock_seq_and_node_.size());
}

Uuid::Octets Uuid::ToOctets() const {
  Octets octets;
  octets[0] = static_cast<uint8_t>(time_low_ >> 24);
  octets[1] = static_cast<uint8_t>(time_low_ >> 16);
  octets[2] = static_cast<uint8_t>(time_low_ >> 8);
  octets[3] = static_cast<uint8_t>(time_low_);
  octets[4] = static_cast<uint8_t>(time_mid_ >> 8);
  octets[5] = static_cast<uint8_t>(time_mid_);
  octets[6] = static_cast<uint8_t>(time_hi_and_version_ >> 8);
  octets[7] = static_cast<uint8_t>(time_hi_and_version_);
  ::memcpy(&octets[8], clock_seq_and_node_.data(), clock_seq_and_node_.size());
  return octets;
}

fbl::StringBuffer<Uuid::kUuidStringLength> Uuid::ToString() const {
  fbl::StringBuffer<kUuidStringLength> str;
  // RFC 4122 specification dictates a UUID is of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  constexpr const char* kUuidNameFormat =
      "%08x-%04x-%04x-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx";
  str.AppendPrintf(kUuidNameFormat, time_low_, time_mid_, time_hi_and_version_,
                   clock_seq_and_node_[0], clock_seq_and_node_[1], clock_seq_and_node_[2],
                   clock_seq_and_node_[3], clock_seq_and_node_[4], clock_seq_and_node_[5],
                   clock_seq_and_node_[6], clock_seq_and_node_[7]);
  return str;
}

bool operator==(const Uuid& l, const Uuid& r) {
  return ((l.time_low_ == r.time_low_) && (l.time_mid_ == r.time_mid_) &&
          (l.time_hi_and_version_ == r.time_hi_and_version_) &&
          (l.clock_seq_and_node_ == r.clock_seq_and_node_));
}

bool operator<(const Uuid& l, const Uuid& r) {
  if (l.time_low_ != r.time_low_) {
    return l.time_low_ < r.time_low_;
  }

  if (l.time_mid_ != r.time_mid_) {
    return l.time_mid_ < r.time_mid_;
  }

  if (l.time_hi_and_version_ != r.time_hi_and_version_) {
    return l.time_hi_and_version_ < r.time_hi_and_version_;
  }

  return l.clock_seq_and_node_ < r.clock_seq_and_node_;
}

}  // namespace optee

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-util.h"

#include <string.h>
#include <zircon/assert.h>

namespace optee {

Uuid::Uuid(const fuchsia_tee::Uuid& uuid) { UuidInternal(uuid); }

Uuid::Uuid(const uuid_t& uuid) { UuidInternal(uuid); }

template <typename T>
void Uuid::UuidInternal(const T& uuid) {
  static_assert(std::is_same_v<T, fuchsia_tee::Uuid> || std::is_same_v<T, uuid_t>);
  data_[0] = static_cast<uint8_t>(uuid.time_low >> 24);
  data_[1] = static_cast<uint8_t>(uuid.time_low >> 16);
  data_[2] = static_cast<uint8_t>(uuid.time_low >> 8);
  data_[3] = static_cast<uint8_t>(uuid.time_low);
  data_[4] = static_cast<uint8_t>(uuid.time_mid >> 8);
  data_[5] = static_cast<uint8_t>(uuid.time_mid);
  data_[6] = static_cast<uint8_t>(uuid.time_hi_and_version >> 8);
  data_[7] = static_cast<uint8_t>(uuid.time_hi_and_version);
  ::memcpy(&data_[8], &uuid.clock_seq_and_node, sizeof(uuid.clock_seq_and_node));
}

void Uuid::ToUint64Pair(uint64_t* out_hi, uint64_t* out_low) const {
  ZX_DEBUG_ASSERT(out_hi);
  ZX_DEBUG_ASSERT(out_low);

  // REE and TEE always share the same endianness so the treatment of UUID bytes is the same on
  // both sides.
  ::memcpy(out_hi, data_, sizeof(*out_hi));
  ::memcpy(out_low, data_ + sizeof(*out_hi), sizeof(*out_low));
}

}  // namespace optee

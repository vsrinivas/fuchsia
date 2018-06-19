// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "types.h"

namespace btlib {
namespace sm {

PairingFeatures::PairingFeatures() { std::memset(this, 0, sizeof(*this)); }

PairingFeatures::PairingFeatures(bool initiator, bool sc, PairingMethod method,
                                 uint8_t enc_key_size, KeyDistGenField local_kd,
                                 KeyDistGenField remote_kd)
    : initiator(initiator),
      secure_connections(sc),
      method(method),
      encryption_key_size(enc_key_size),
      local_key_distribution(local_kd),
      remote_key_distribution(remote_kd) {}

SecurityProperties::SecurityProperties()
    : level_(SecurityLevel::kNoSecurity), enc_key_size_(0u), sc_(false) {}

SecurityProperties::SecurityProperties(SecurityLevel level, size_t enc_key_size,
                                       bool secure_connections)
    : level_(level), enc_key_size_(enc_key_size), sc_(secure_connections) {}

}  // namespace sm
}  // namespace btlib

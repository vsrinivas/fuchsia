// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "types.h"

#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace sm {
namespace {

std::string LevelToString(SecurityLevel level) {
  switch (level) {
    case SecurityLevel::kEncrypted:
      return "encrypted";
    case SecurityLevel::kAuthenticated:
      return "encrypted (MITM)";
    default:
      break;
  }
  return "insecure";
}

}  // namespace

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

std::string SecurityProperties::ToString() const {
  return fxl::StringPrintf(
      "[security: %s, key size: %lu, %s]", LevelToString(level()).c_str(),
      enc_key_size(), secure_connections() ? "secure conn." : "legacy pairing");
}

LTK::LTK(const SecurityProperties& security, const hci::LinkKey& key)
    : security_(security), key_(key) {}

}  // namespace sm
}  // namespace btlib

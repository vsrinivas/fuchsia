// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/uint128.h"
#include "garnet/drivers/bluetooth/lib/hci/link_key.h"
#include "garnet/drivers/bluetooth/lib/sm/smp.h"

namespace btlib {
namespace sm {

// Represents the features exchanged during Pairing Phase 1.
struct PairingFeatures final {
  PairingFeatures();
  PairingFeatures(bool initiator, bool sc, PairingMethod method,
                  uint8_t enc_key_size, KeyDistGenField local_kd,
                  KeyDistGenField remote_kd);

  // True if the local device is in the "initiator" role.
  bool initiator;

  // True if LE Secure Connections pairing should be used. Otherwise, LE Legacy
  // Pairing should be used.
  bool secure_connections;

  // Indicates the key generation model used for Phase 2.
  PairingMethod method;

  // The negotiated encryption key size.
  uint8_t encryption_key_size;

  // The keys that we must distribute to the peer.
  KeyDistGenField local_key_distribution;

  // The keys that will be distributed to us by the peer.
  KeyDistGenField remote_key_distribution;
};

enum class SecurityLevel {
  // No encryption
  kNoSecurity = 0,

  // Encrypted without MITM protection (unauthenticated)
  kEncrypted = 1,

  // Encrypted with MITM protection (authenticated)
  kAuthenticated = 2,
};

// Represents the security properties of a key. The security properties of a
// connection's LTK defines the security properties of the connection.
class SecurityProperties final {
 public:
  SecurityProperties();
  SecurityProperties(SecurityLevel level, size_t enc_key_size,
                     bool secure_connections);

  SecurityLevel level() const { return level_; }
  size_t enc_key_size() const { return enc_key_size_; }
  bool secure_connections() const { return sc_; }
  bool authenticated() const { return level_ == SecurityLevel::kAuthenticated; }

  // Returns a string representation of these properties.
  std::string ToString() const;

  // Compare two properties for equality.
  inline bool operator==(const SecurityProperties& other) const {
    return level_ == other.level_ && enc_key_size_ == other.enc_key_size_ &&
           sc_ == other.sc_;
  }

 private:
  SecurityLevel level_;
  size_t enc_key_size_;
  bool sc_;
};

// Represents a Long Term Key.
class LTK final {
 public:
  LTK() = default;
  LTK(const SecurityProperties& security, const hci::LinkKey& key);

  const SecurityProperties& security() const { return security_; }
  const hci::LinkKey& key() const { return key_; }

 private:
  SecurityProperties security_;
  hci::LinkKey key_;
};

// Represents a 128-bit key.
class Key final {
 public:
  Key() = default;
  Key(const SecurityProperties& security, const common::UInt128& value);

  const SecurityProperties& security() const { return security_; }
  const common::UInt128& value() const { return value_; }

 private:
  SecurityProperties security_;
  common::UInt128 value_;
};

}  // namespace sm
}  // namespace btlib

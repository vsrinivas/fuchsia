// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_

#include <iostream>
#include <vector>

namespace p2p_provider {
// Type describing whether the change is a new device or a deleted one.
enum class DeviceChangeType : bool { NEW, DELETED };

// Opaque handle to a peer-to-peer client identifier.
class P2PClientId {
 public:
  explicit P2PClientId(std::vector<uint8_t> data);

  P2PClientId(const P2PClientId& other) = default;
  P2PClientId& operator=(const P2PClientId& other) = default;

  P2PClientId(P2PClientId&& other) = default;
  P2PClientId& operator=(P2PClientId&& other) = default;

  bool operator==(const P2PClientId& other) const;
  bool operator!=(const P2PClientId& other) const;
  bool operator<(const P2PClientId& other) const;

  // Returns the underlying client id data, as an opaque blob.
  const std::vector<uint8_t>& GetData() const;

 private:
  friend std::ostream& operator<<(std::ostream& os, const P2PClientId& client_id);

  std::vector<uint8_t> data_;
};

std::ostream& operator<<(std::ostream& os, const P2PClientId& client_id);

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_

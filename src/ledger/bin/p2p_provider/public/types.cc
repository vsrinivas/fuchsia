// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/public/types.h"

#include "src/ledger/lib/convert/convert.h"

namespace p2p_provider {
P2PClientId::P2PClientId(std::vector<uint8_t> data) : data_(std::move(data)) {}

bool P2PClientId::operator==(const P2PClientId& other) const { return data_ == other.data_; }

bool P2PClientId::operator!=(const P2PClientId& other) const { return !(*this == other); }

bool P2PClientId::operator<(const P2PClientId& other) const { return data_ < other.data_; }

const std::vector<uint8_t>& P2PClientId::GetData() const { return data_; }

std::ostream& operator<<(std::ostream& os, const P2PClientId& client_id) {
  return os << convert::ToHex(client_id.data_);
}

}  // namespace p2p_provider

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOCKS_PUBLIC_TYPES_H_
#define SRC_LEDGER_BIN_CLOCKS_PUBLIC_TYPES_H_

#include <string>

namespace clocks {

using DeviceFingerprint = std::string;

// An identifier for a device interested in a page.
struct DeviceId {
  DeviceFingerprint fingerprint;
  uint64_t epoch;
};

bool operator==(const DeviceId& lhs, const DeviceId& rhs);
bool operator!=(const DeviceId& lhs, const DeviceId& rhs);
bool operator<(const DeviceId& lhs, const DeviceId& rhs);
std::ostream& operator<<(std::ostream& os, const DeviceId& e);

}  // namespace clocks

#endif  // SRC_LEDGER_BIN_CLOCKS_PUBLIC_TYPES_H_

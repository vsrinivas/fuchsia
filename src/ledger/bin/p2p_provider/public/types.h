// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_

namespace p2p_provider {
// Type describing whether the change is a new device or a deleted one.
enum class DeviceChangeType : bool { NEW, DELETED };

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_PUBLIC_TYPES_H_

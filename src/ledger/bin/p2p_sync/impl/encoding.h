// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_IMPL_ENCODING_H_
#define SRC_LEDGER_BIN_P2P_SYNC_IMPL_ENCODING_H_

#include "src/ledger/bin/p2p_sync/impl/message_generated.h"
#include "src/ledger/lib/convert/convert.h"

namespace p2p_sync {

// Parses |data| into a |Message| object. Returns nullptr if the message is
// malformed. The returned pointer is valid as long as |data|'s data is valid.
const Message* ParseMessage(convert::ExtendedStringView data);

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_IMPL_ENCODING_H_

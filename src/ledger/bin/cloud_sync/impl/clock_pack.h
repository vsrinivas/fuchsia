// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CLOCK_PACK_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CLOCK_PACK_H_

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace cloud_sync {

// Encodes a Clock into a ClockPack.
cloud_provider::ClockPack EncodeClock(const storage::Clock& clock);

// Decodes a ClockPack into a Clock, assuming the ClockPack comes from the cloud provider. Returns
// false if the decoding fails.
bool DecodeClock(cloud_provider::ClockPack clock_pack, storage::Clock* clock);

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CLOCK_PACK_H_

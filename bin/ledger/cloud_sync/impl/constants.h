// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_CONSTANTS_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_CONSTANTS_H_

#include <lib/fxl/strings/string_view.h>

namespace cloud_sync {

// Key for the timestamp metadata in the SyncMetadata KV store.
constexpr fxl::StringView kTimestampKey = "timestamp";

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_CONSTANTS_H_

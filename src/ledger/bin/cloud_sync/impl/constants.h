// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CONSTANTS_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CONSTANTS_H_

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

// Key for the timestamp metadata in the SyncMetadata KV store.
inline constexpr absl::string_view kTimestampKey = "timestamp";

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_CONSTANTS_H_

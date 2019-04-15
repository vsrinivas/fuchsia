// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_CONSTANTS_H_
#define SRC_LEDGER_BIN_APP_CONSTANTS_H_

#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

// The maximum key size.
inline constexpr size_t kMaxKeySize = 256;

// The root Page ID.
extern const fxl::StringView kRootPageId;

// Filename under which the server id used to sync a given user is stored within
// the repository dir of that user.
inline constexpr fxl::StringView kServerIdFilename = "server_id";

// The serialization version of PageUsage DB.
inline constexpr fxl::StringView kPageUsageDbSerializationVersion = "1";

inline constexpr char kRepositoriesInspectPathComponent[] = "repositories";
// TODO(nathaniel): "requests" was introduced as a demonstration; it should be
// either given real meaning or cleaned up.
inline constexpr char kRequestsInspectPathComponent[] = "requests";
inline constexpr char kLedgersInspectPathComponent[] = "ledgers";

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_CONSTANTS_H_

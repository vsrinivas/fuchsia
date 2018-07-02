// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_CONSTANTS_H_
#define PERIDOT_BIN_LEDGER_APP_CONSTANTS_H_

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// The size of a page id array.
constexpr size_t kPageIdSize = 16;

// The maximum key size.
constexpr size_t kMaxKeySize = 256;

// The root id. The array size must be equal to |kPageIdSize|.
extern const fxl::StringView kRootPageId;

// Filename under which the server id used to sync a given user is stored within
// the repository dir of that user.
constexpr fxl::StringView kServerIdFilename = "server_id";

// The serialization version of PageUsage DB.
constexpr fxl::StringView kPageUsageDbSerializationVersion = "1";

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_CONSTANTS_H_

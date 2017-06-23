// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_CONSTANTS_H_
#define APPS_LEDGER_SRC_APP_CONSTANTS_H_

#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/strings/string_view.h"

namespace ledger {

// The size of a page id array.
constexpr size_t kPageIdSize = 16;

// The root id. The array size must be equal to |kPageIdSize|.
extern const ftl::StringView kRootPageId;

// The file path under which the last user id for which the Ledger was
// initialized, is stored. This is a stop-gap convenience solution to allow
// `cloud_sync clean` to reset the ledger for the concrete user on the device
// (and not wipe the entire cloud which can be shared between many users).
constexpr ftl::StringView kLastUserIdPath = "/data/ledger/last_user_id";
constexpr ftl::StringView kLastUserRepositoryPath =
    "/data/ledger/last_user_dir";

// Filename under which the server id used to sync a given user is stored within
// the repository dir of that user.
constexpr ftl::StringView kServerIdFilename = "server_id";

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_CONSTANTS_H_

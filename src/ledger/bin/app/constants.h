// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_CONSTANTS_H_
#define SRC_LEDGER_BIN_APP_CONSTANTS_H_

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// The maximum key size.
inline constexpr size_t kMaxKeySize = 256;

// The root Page ID.
extern const absl::string_view kRootPageId;

// Filename under which the server id used to sync a given user is stored within
// the repository dir of that user.
inline constexpr absl::string_view kServerIdFilename = "server_id";

// The serialization version of the repository DB.
inline constexpr absl::string_view kRepositoryDbSerializationVersion = "2";

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_CONSTANTS_H_

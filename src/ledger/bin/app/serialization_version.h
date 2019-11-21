// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_SERIALIZATION_VERSION_H_
#define SRC_LEDGER_BIN_APP_SERIALIZATION_VERSION_H_

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// The serialization version of anything Ledger stores on local storage
// (directory structure, object/LevelDb serialization).
inline constexpr absl::string_view kSerializationVersion = "38";

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_SERIALIZATION_VERSION_H_

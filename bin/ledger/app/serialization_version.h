// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_SERIALIZATION_VERSION_H_
#define PERIDOT_BIN_LEDGER_APP_SERIALIZATION_VERSION_H_

namespace ledger {

// The serialization version of anything Ledger stores on local storage
// (directory structure, object/LevelDb serialization).
constexpr fxl::StringView kSerializationVersion = "29";

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_SERIALIZATION_VERSION_H_

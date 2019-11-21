// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_SERIALIZATION_H_
#define SRC_LEDGER_BIN_APP_SERIALIZATION_H_

#include <string>

namespace ledger {

enum class RepositoryRowPrefix : char {
  PAGE_USAGE_DB = ' ',
  CLOCKS,  // '!'
};

std::string ToString(RepositoryRowPrefix prefix);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_SERIALIZATION_H_

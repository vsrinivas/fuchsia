// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/serialization.h"

namespace ledger {
std::string ToString(RepositoryRowPrefix prefix) { return {static_cast<char>(prefix)}; }

}  // namespace ledger

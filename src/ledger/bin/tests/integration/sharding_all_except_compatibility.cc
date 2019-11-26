// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/integration/sharding.h"

namespace ledger {

IntegrationTestShard GetIntegrationTestShard() {
  return IntegrationTestShard::ALL_EXCEPT_DIFF_COMPATIBILITY;
}

}  // namespace ledger

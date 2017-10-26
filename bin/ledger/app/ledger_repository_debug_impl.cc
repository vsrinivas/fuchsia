// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_debug_impl.h"

#include "lib/fxl/logging.h"

namespace ledger {

void LedgerRepositoryDebugImpl::GetInt(const GetIntCallback& callback) {
  callback(259);
  return;
}

}  // namespace ledger

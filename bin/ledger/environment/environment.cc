// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace ledger {

Environment::Environment(async_t* async)
    : async_(async),
      coroutine_service_(std::make_unique<coroutine::CoroutineServiceImpl>()) {
  FXL_DCHECK(async_);
}

Environment::~Environment() {}

}  // namespace ledger

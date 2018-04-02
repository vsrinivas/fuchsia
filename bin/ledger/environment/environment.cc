// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "lib/fxl/macros.h"

namespace ledger {

Environment::Environment(fxl::RefPtr<fxl::TaskRunner> main_runner,
                         async_t* async)
    : main_runner_(std::move(main_runner)),
      async_(async),
      coroutine_service_(std::make_unique<coroutine::CoroutineServiceImpl>()) {
  FXL_DCHECK(main_runner_);
}

Environment::~Environment() {}

}  // namespace ledger

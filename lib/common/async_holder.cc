// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/common/async_holder.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>
#include <lib/zx/time.h>

namespace modular {

AsyncHolderBase::AsyncHolderBase(std::string name)
    : name_(std::move(name)), down_(std::make_shared<bool>(false)) {}

AsyncHolderBase::~AsyncHolderBase() {
  if (!*down_) {
    // This is not a warning because it happens because of an outer timeout, for
    // which there already is a warning issued.
    FXL_DLOG(INFO) << "Delete without teardown: " << name_;
  }
  *down_ = true;
}

void AsyncHolderBase::Teardown(fxl::TimeDelta timeout,
                               std::function<void()> done) {
  auto cont = [this, down = down_,
               done = std::move(done)](const bool from_timeout) {
    if (*down) {
      return;
    }

    *down = true;

    if (from_timeout) {
      FXL_LOG(WARNING) << "Teardown() timed out for " << name_;
    }

    ImplReset();

    done();
  };

  auto cont_timeout = [cont] { cont(true); };

  auto cont_normal = [cont] { cont(false); };

  async::PostDelayedTask(async_get_default(), cont_timeout,
                         zx::nsec(timeout.ToNanoseconds()));
  ImplTeardown(cont_normal);
}

}  // namespace modular

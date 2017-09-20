// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/common/async_holder.h"

#include "lib/fxl/tasks/task_runner.h"
#include "lib/fsl/tasks/message_loop.h"

namespace modular {

AsyncHolderBase::AsyncHolderBase(const char* const name)
    : name_(name) {}

AsyncHolderBase::~AsyncHolderBase() = default;

void AsyncHolderBase::Teardown(fxl::TimeDelta timeout, std::function<void()> done) {
  // TODO(mesch): There is duplication with code in
  // AppClientBase::AppTerminate(). Should be unified.
  auto called = std::make_shared<bool>(false);
  auto cont = [this, called, done = std::move(done)](const bool from_timeout) {
    if (*called) {
      return;
    }

    *called = true;

    if (from_timeout) {
      FXL_LOG(WARNING) << "Teardown() timed out for " << name_;
    }

    ImplReset();

    done();
  };

  auto cont_timeout = [cont] {
    cont(true);
  };

  auto cont_normal = [cont] {
    cont(false);
  };

  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(cont_timeout, timeout);
  ImplTeardown(cont_normal);
}

}  // namespace modular

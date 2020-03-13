// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/common/async_holder.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

AsyncHolderBase::AsyncHolderBase(std::string name)
    : name_(std::move(name)), down_(std::make_shared<bool>(false)) {}

AsyncHolderBase::~AsyncHolderBase() {
  if (!*down_) {
    // This is not a warning because it happens because of an outer timeout, for
    // which there already is a warning issued.
    FX_DLOGS(INFO) << "Delete without teardown: " << name_;
  }
  *down_ = true;
}

void AsyncHolderBase::Teardown(zx::duration timeout, fit::function<void()> done) {
  fit::callback<void(bool)> cont = [this, down = down_,
                                    done = std::move(done)](const bool from_timeout) mutable {
    if (*down) {  // |down| shared_ptr prevents using |this| after destruct
      return;
    }

    *down = true;

    if (from_timeout) {
      FX_LOGS(WARNING) << "Teardown() timed out for " << name_;
    }

    ImplReset();

    done();
  };

  auto cont_timeout = [cont = cont.share()]() mutable {
    if (cont)
      cont(true);
  };

  auto cont_normal = [cont = std::move(cont)]() mutable {
    if (cont)
      cont(false);
  };

  async::PostDelayedTask(async_get_default_dispatcher(), std::move(cont_timeout), timeout);
  ImplTeardown(std::move(cont_normal));
}

ClosureAsyncHolder::ClosureAsyncHolder(std::string name,
                                       fit::function<void(DoneCallback)> on_teardown)
    : AsyncHolderBase(name), on_teardown_(std::move(on_teardown)), on_reset_([]() {}) {}

ClosureAsyncHolder::ClosureAsyncHolder(std::string name,
                                       fit::function<void(DoneCallback)> on_teardown,
                                       fit::function<void()> on_reset)
    : AsyncHolderBase(name), on_teardown_(std::move(on_teardown)), on_reset_(std::move(on_reset)) {}

ClosureAsyncHolder::~ClosureAsyncHolder() = default;

void ClosureAsyncHolder::ImplTeardown(fit::function<void()> done) {
  on_teardown_(std::move(done));
};

void ClosureAsyncHolder::ImplReset() { on_reset_(); };

}  // namespace modular

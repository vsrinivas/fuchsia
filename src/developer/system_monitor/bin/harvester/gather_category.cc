// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/time.h>
#include <zircon/status.h>

#include "gather_cpu.h"
#include "harvester.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

std::string ZxErrorString(const std::string& cmd, zx_status_t err) {
  std::ostringstream os;
  os << cmd << " returned " << zx_status_get_string(err) << " (" << err << ")";
  return os.str();
}

void GatherCategory::PostUpdate(async_dispatcher_t* dispatcher, zx::time start,
                                zx::duration period) {
  task_method_.Cancel();
  update_period_ = period;
  zx::duration delta = start - next_update_;
  // Round up to the next multiple of |update_period_|. This removes drift that
  // may otherwise occur with fixed sleep time. If an update takes longer than
  // the |update_period_| then some updates will be skipped.
  next_update_ += update_period_ * (delta / update_period_ + 1LL);
  task_method_.PostForTime(dispatcher, next_update_);
}

void GatherCategory::TaskHandler(async_dispatcher_t* dispatcher,
                                 async::TaskBase* /*task*/,
                                 zx_status_t /*status*/) {
  Gather();
  PostUpdate(dispatcher, async::Now(dispatcher), update_period_);
};

}  // namespace harvester

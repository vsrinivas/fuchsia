// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/executor.h>

void Executor::Init() {
  fbl::AllocChecker ac;
  root_job_observer_ = ktl::make_unique<RootJobObserver>(&ac);
  if (!ac.check()) {
    panic("root-job: failed to allocate observer\n");
  }
}

bool Executor::KillJobWithKillOnOOM() { return root_job_observer_->KillJobWithKillOnOOM(); }

fbl::RefPtr<JobDispatcher> Executor::GetRootJobDispatcher() {
  return root_job_observer_->GetRootJobDispatcher();
}

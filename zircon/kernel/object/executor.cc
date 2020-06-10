// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/executor.h>

void Executor::Init() {
  // Create root job.
  root_job_ = JobDispatcher::CreateRootJob();

  // Watch the root job, alerting if it ever ends up with no children.
  fbl::AllocChecker ac;
  root_job_observer_ = ktl::make_unique<RootJobObserver>(&ac, root_job_);
  if (!ac.check()) {
    panic("root-job: failed to allocate observer\n");
  }

  // Initialize the memory watchdog.
  memory_watchdog_.Init(this);
}

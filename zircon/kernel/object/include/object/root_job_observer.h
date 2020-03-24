// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_

#include <object/job_dispatcher.h>
#include <object/state_observer.h>

class RootJobObserver final : public StateObserver {
 public:
  RootJobObserver() : root_job_(JobDispatcher::CreateRootJob()) { root_job_->AddObserver(this); }

  bool KillJobWithKillOnOOM() { return root_job_->KillJobWithKillOnOOM(); }

  fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() { return root_job_; }

 private:
  Flags OnInitialize(zx_signals_t initial_state, const CountInfo* cinfo) final;
  Flags OnStateChange(zx_signals_t new_state) final;
  Flags OnCancel(const Handle* handle) final;

  fbl::RefPtr<JobDispatcher> root_job_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_

// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_

#include <fbl/function.h>
#include <object/job_dispatcher.h>
#include <object/signal_observer.h>

class RootJobObserver final : public SignalObserver {
 public:
  ~RootJobObserver() final;

  // Create a RootJobObserver that halts the system when the root job terminates
  // (i.e. asserts ZX_JOB_NO_CHILDREN).
  RootJobObserver(fbl::RefPtr<JobDispatcher> root_job, Handle* root_job_handle);

  // Create a RootJobObserver that calls the given callback when the root job
  // terminates (i.e. asserts ZX_JOB_NO_CHILDREN).
  //
  // The callback is called while holding the watched JobDispatcher's lock, so
  // the callback must avoid calling anything that may attempt to acquire that
  // lock again, introduce a lock cycle, etc.
  //
  // Exposed for testing.
  RootJobObserver(fbl::RefPtr<JobDispatcher> root_job, Handle* root_job_handle,
                  fbl::Closure callback);

 private:
  // |SignalObserver| implementation.
  void OnMatch(zx_signals_t signals) final;
  void OnCancel(zx_signals_t signals) final;

  fbl::RefPtr<JobDispatcher> root_job_;
  fbl::Closure callback_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_ROOT_JOB_OBSERVER_H_

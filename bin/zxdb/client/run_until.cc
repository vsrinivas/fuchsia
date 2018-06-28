// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/run_until.h"

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_controller.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/session.h"
//#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/macros.h"

namespace zxdb {

namespace {

using Callback = std::function<void(const Err&)>;

// This class corresponds to an invocation of one "run until" command. Under
// the current design "run until" is something the user triggers that's
// associated with a thread or process. It is not owned by any particular
// object: it watches for the appropriate thread or process changes and deletes
// itself when the operation is no longer needed.
//
// TODO(brettw) this will need to be revisited when there are more thread
// control primitives. It could be that the process step case is completely
// different than the thread step case. If we have a system for managing the
// lifetimes and ownership of thread commands, the thread version should use it
// In that case, the process "until" command could be a special thing, or
// possibly it should just create a user-visible one-shot breakpoint.
class RunUntilHelper : public ProcessObserver,
                       public TargetObserver,
                       public BreakpointController {
 public:
  RunUntilHelper(Process* process, InputLocation location, Callback cb);
  RunUntilHelper(Thread* thread, InputLocation location, Callback cb);
  virtual ~RunUntilHelper();

  // ProcessObserver implementation:
  void WillDestroyThread(Process* process, Thread* thread) override;

  // TargetObserver implementation:
  void WillDestroyProcess(Target* target, Process* process,
                          TargetObserver::DestroyReason reason,
                          int exit_code) override;

  // BreakpointController implementation:
  BreakpointAction GetBreakpointHitAction(Breakpoint* bp,
                                          Thread* thread) override;

  // Callback when the breakpoint set is complete.
  void OnSetComplete(const Err& err);

  // All deletion of this class should go through this function to avoid
  // double-deletes.
  void ScheduleDelete();

private:
  System* system_;

  // Only one of process_ or thread_ will be non-null, according to what
  // type of object this operation is associated with.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;

  Callback set_callback_;

  fxl::WeakPtr<Breakpoint> breakpoint_;

  // Set when an asynchronous deletion is scheduled. We should not schedule
  // another if this is set.
  bool pending_delete_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(RunUntilHelper);
};

RunUntilHelper::RunUntilHelper(Process* process, InputLocation location,
                               Callback cb)
    : system_(&process->session()->system()),
      process_(process),
      set_callback_(std::move(cb)) {
  Target* target = process->GetTarget();
  target->AddObserver(this);

  breakpoint_ = process->session()
                    ->system()
                    .CreateNewInternalBreakpoint(this)
                    ->GetWeakPtr();

  BreakpointSettings settings;
  settings.scope = BreakpointSettings::Scope::kTarget;
  settings.scope_target = target;
  settings.location = std::move(location);
  settings.one_shot = true;

  breakpoint_->SetSettings(settings,
                           [this](const Err& err) { OnSetComplete(err); });
}

RunUntilHelper::RunUntilHelper(Thread* thread, InputLocation location,
                               Callback cb)
    : system_(&thread->session()->system()),
      thread_(thread),
      set_callback_(std::move(cb)) {
  Process* process = thread->GetProcess();
  process->AddObserver(this);

  breakpoint_ = thread->session()
                    ->system()
                    .CreateNewInternalBreakpoint(this)
                    ->GetWeakPtr();

  BreakpointSettings settings;
  settings.scope = BreakpointSettings::Scope::kThread;
  settings.scope_target = process->GetTarget();
  settings.scope_thread = thread;
  settings.location = std::move(location);
  settings.one_shot = true;

  breakpoint_->SetSettings(settings,
                           [this](const Err& err) { OnSetComplete(err); });
}

RunUntilHelper::~RunUntilHelper() {
  if (thread_) {
    FXL_DCHECK(!process_);
    thread_->GetProcess()->RemoveObserver(this);
  } else if (process_) {
    process_->GetTarget()->RemoveObserver(this);
  }
  if (breakpoint_)
    system_->DeleteBreakpoint(breakpoint_.get());
}

void RunUntilHelper::WillDestroyThread(Process* process, Thread* thread) {
  // Should be a thread-scoped operation to be registered for this.
  FXL_DCHECK(thread_);
  thread_ = nullptr;

  // Thread is gone, our job is done. The destructor will unregister the
  // breakpoint.
  ScheduleDelete();
}

void RunUntilHelper::WillDestroyProcess(Target* target, Process* process,
                                        TargetObserver::DestroyReason reason,
                                        int exit_code) {
  // Should be a process_-scoped operation to be registered for this.
  FXL_DCHECK(process_);
  process_ = nullptr;

  // Thread is gone, our job is done. The destructor will unregister the
  // breakpoint.
  ScheduleDelete();
}

BreakpointAction RunUntilHelper::GetBreakpointHitAction(Breakpoint* bp,
                                                        Thread* thread) {
  ScheduleDelete();
  return BreakpointAction::kStop;
}

void RunUntilHelper::OnSetComplete(const Err& err) {
  // Forward error to original requestor of the command.
  set_callback_(err);

  // Prevent accidentally issuing the callback again.
  set_callback_ = Callback();

  if (err.has_error()) {
    // The breakpoint was not set so delete our object.
    ScheduleDelete();
  } else {
    // Now that the breakpoint is ready we can continue. Watch out, the thread
    // or process could have been deleted at this point.
    if (thread_)
      thread_->Continue();
    else if (process_)
      process_->Continue();
  }
}

void RunUntilHelper::ScheduleDelete() {
  // This can get called multiple times (e.g. breakpoint is hit and thread exit
  // happens before posted task is run), ensure we only delete once.
  if (!pending_delete_) {
    pending_delete_ = true;
    debug_ipc::MessageLoop::Current()->PostTask([this]() { delete this; });
  }
}

}  // namespace

void RunUntil(Process* process, InputLocation location,
              std::function<void(const Err&)> cb) {
  new RunUntilHelper(process, location, std::move(cb));
}

void RunUntil(Thread* thread, InputLocation location,
              std::function<void(const Err&)> cb) {
  new RunUntilHelper(thread, location, std::move(cb));
}

}  // namespace zxdb

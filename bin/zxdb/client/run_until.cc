// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/run_until.h"

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_controller.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/macros.h"

namespace zxdb {

namespace {

using Callback = std::function<void(const Err&)>;

class RunUntilHelper;

// Class whose only purpose it to serve as a nominal owner for RunUntilHelpers
// instances. See RunUntilHelper class comment for more information.
// Implementation is at the end for clarity.
class RunUntilHolder {
 public:
  // Only one global instance is needed
  static RunUntilHolder& Get();

  // This will set the RunUntilHelper Id
  void AddRunUntilHelper(std::unique_ptr<RunUntilHelper>);
  void DeleteRunUntilHelper(uint32_t id);

 private:
  RunUntilHolder() = default;

  uint32_t next_helper_id_ = 0;
  std::map<int, std::unique_ptr<RunUntilHelper>> helpers_map_;
};

// This class corresponds to an invocation of one "run until" command. Under
// the current design "run until" is something the user triggers that's
// associated with a thread or process.
//
// Conceptually, it is not owned by any particular object: it watches for the
// appropriate thread or process changes and schedules itself for deletion when
// the operation is no longer needed. In practice, is owned by a manager object
// whose only purpose is to hold these self-managed objects. This is because
// memory checking tools (eg. ASAN) get tripped up the having un-owned new
// calls.
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
  RunUntilHelper(Process* process, InputLocation location,
                 Callback cb);

  // Non-zero frame SPs will check the current frame's SP and only trigger the
  // breakpoint when it matches. A zero SP will ignore the stack and always
  // trigger at the location.
  RunUntilHelper(Thread* thread, InputLocation location,
                 uint64_t frame_sp, Callback cb);

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

  uint32_t id() { return id_; }
  void set_id(uint32_t id) { id_ = id; }

 private:
  System* system_;

  // Only one of process_ or thread_ will be non-null, according to what
  // type of object this operation is associated with.
  Process* process_ = nullptr;
  Thread* thread_ = nullptr;

  uint64_t frame_sp_ = 0;

  Callback set_callback_;

  fxl::WeakPtr<Breakpoint> breakpoint_;

  // Set when an asynchronous deletion is scheduled. We should not schedule
  // another if this is set.
  bool pending_delete_ = false;
  uint32_t id_;   // Set up by RunUntilHolder.

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
                               uint64_t frame_sp, Callback cb)
    : system_(&thread->session()->system()),
      thread_(thread),
      frame_sp_(frame_sp),
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

  // Frame-tied triggers can't be one-shot because we need to check the stack
  // every time it triggers. In the non-frame case the one-shot breakpoint will
  // be slightly more efficient.
  settings.one_shot = frame_sp_ == 0;

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
  FXL_DCHECK(bp == breakpoint_.get());
  if (!frame_sp_) {
    // Always stop, not frame specific.
    ScheduleDelete();
    return BreakpointAction::kStop;
  }

  auto frames = thread->GetFrames();
  if (frames.empty()) {
    FXL_NOTREACHED();  // Should always have a current frame on stop.
    return BreakpointAction::kContinue;
  }

  // The stack grows downward. Want to stop the thread only when the frame is
  // before (greater than) the input one, which means anything <= should
  // continue.
  if (frames[0]->GetStackPointer() <= frame_sp_)
    return BreakpointAction::kContinue;

  // Got a match. We want to delete the breakpoint but can't because it's the
  // object that just called into us. Disable it for now and schedule
  // everything for deletion in the future.
  BreakpointSettings settings = bp->GetSettings();
  settings.enabled = false;
  bp->SetSettings(settings, [](const Err&) {});

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
    debug_ipc::MessageLoop::Current()->PostTask([this]() {
        RunUntilHolder::Get().DeleteRunUntilHelper(this->id());
    });
  }
}

// RunUntilHelper definition ---------------------------------------------------

RunUntilHolder& RunUntilHolder::Get() {
  static RunUntilHolder holder;
  return holder;
}

void RunUntilHolder::AddRunUntilHelper(std::unique_ptr<RunUntilHelper> helper) {
  helper->set_id(next_helper_id_++);
  helpers_map_[helper->id()] = std::move(helper);
}

void RunUntilHolder::DeleteRunUntilHelper(uint32_t id) {
  helpers_map_.erase(id);
}

}  // namespace

// Public interface ------------------------------------------------------------

void RunUntil(Process* process, InputLocation location,
              std::function<void(const Err&)> cb) {
  auto h = std::make_unique<RunUntilHelper>(process, location, std::move(cb));
  RunUntilHolder::Get().AddRunUntilHelper(std::move(h));
}

void RunUntil(Thread* thread, InputLocation location,
              std::function<void(const Err&)> cb) {
  auto h = std::make_unique<RunUntilHelper>(thread, location, 0, std::move(cb));
  RunUntilHolder::Get().AddRunUntilHelper(std::move(h));
}

void RunUntil(Thread* thread, InputLocation location, uint64_t frame_sp,
              std::function<void(const Err&)> cb) {
  auto h = std::make_unique<RunUntilHelper>(thread, location, frame_sp,
                                            std::move(cb));
  RunUntilHolder::Get().AddRunUntilHelper(std::move(h));
}

}  // namespace zxdb

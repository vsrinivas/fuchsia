// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/thread_observer.h"

namespace zxdb {

class Breakpoint;
class Command;
class Session;

// The context for console commands. In a model-view-controller UI, this would
// represent the state associated with the view and controller (depending on
// how one splits things up). It keeps track of the currently selected
// objects and watches for changes.
//
// This class maintains the mapping between objects and IDs.
class ConsoleContext
    : public ProcessObserver, public SystemObserver, public TargetObserver,
      public ThreadObserver, public BreakpointObserver {
 public:
  explicit ConsoleContext(Session* session);
  ~ConsoleContext();

  Session* session() { return session_; }

  // Returns the ID for the object. Asserts and returns 0 if not found.
  int IdForTarget(const Target* target) const;
  int IdForThread(const Thread* thread) const;
  int IdForBreakpoint(const Breakpoint* breakpoint) const;

  // The active target will always exist except during setup and teardown.
  void SetActiveTarget(const Target* target);
  int GetActiveTargetId() const;
  Target* GetActiveTarget() const;

  // The active thread for its target. The active target is not affected. The
  // active thread ID for a target not running will be 0.
  void SetActiveThreadForTarget(const Thread* thread);
  int GetActiveThreadIdForTarget(const Target* target);

  // Sets the active breakpoint. Can be null/0 if there is no active breakpoint
  // (set to null to clear).
  void SetActiveBreakpoint(const Breakpoint* breakpoint);
  int GetActiveBreakpointId() const;
  Breakpoint* GetActiveBreakpoint() const;

  // Fills the current effective process, thread, etc. into the given Command
  // structure based on what the command specifies and the current context.
  // Returns an error if any of the referenced IDs are invalid.
  Err FillOutCommand(Command* cmd);

 private:
  struct TargetRecord {
    int target_id = 0;
    Target* target = nullptr;

    int next_thread_id = 1;

    // The active ID will be 0 when there is no active thread (the case when
    // the process is not running).
    int active_thread_id = 0;

    std::map<int, Thread*> id_to_thread;
    std::map<const Thread*, int> thread_to_id;
  };

  // SystemObserver implementation:
  void DidCreateTarget(Target* target) override;
  void WillDestroyTarget(Target* target) override;
  void DidCreateBreakpoint(Breakpoint* breakpoint) override;
  void WillDestroyBreakpoint(Breakpoint* breakpoint) override;

  // TargetObserver implementation:
  void DidCreateProcess(Target* target, Process* process) override;
  void DidDestroyProcess(Target* target, DestroyReason reason,
                         int exit_code) override;

  // ProcessObserver implementation:
  void DidCreateThread(Process* process, Thread* thread) override;
  void WillDestroyThread(Process* process, Thread* thread) override;

  // ThreadObserver implementation:
  void OnThreadStopped(Thread* thread) override;

  // Returns the record for the given target, or null (+ assertion) if not
  // found. These pointers are not stable across target list changes.
  TargetRecord* GetTargetRecord(int target_id);
  TargetRecord* GetTargetRecord(const Target* target);

  Session* const session_;

  // The ID from a user perspective maps to a Target/Process pair.
  std::map<int, TargetRecord> id_to_target_;
  std::map<const Target*, int> target_to_id_;
  int next_target_id_ = 1;

  std::map<int, Breakpoint*> id_to_breakpoint_;
  std::map<const Breakpoint*, int> breakpoint_to_id_;
  int next_breakpoint_id_ = 1;

  int active_target_id_ = 0;
  int active_breakpoint_id_ = 0;
};

}  // namespace zxdb

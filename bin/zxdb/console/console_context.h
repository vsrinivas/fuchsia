// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/thread_observer.h"

namespace zxdb {

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
      public ThreadObserver {
 public:
  explicit ConsoleContext(Session* session);
  ~ConsoleContext();

  Session* session() { return session_; }

  // Returns the ID for the target. Asserts and returns 0 if not found.
  int IdForTarget(Target* target) const;
  int IdForThread(Thread* thread) const;

  // The active target will always exist except during setup and teardown.
  void SetActiveTarget(Target* target);
  int GetActiveTargetId();
  Target* GetActiveTarget();

  // The active thread for its target. The active target is not affected. The
  // active thread ID for a target not running will be 0.
  void SetActiveThreadForTarget(Thread* thread);
  int GetActiveThreadIdForTarget(Target* target);

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
    std::map<Thread*, int> thread_to_id;
  };

  // SystemObserver implementation:
  void DidCreateTarget(Target* target) override;
  void WillDestroyTarget(Target* target) override;

  // TargetObserver implementation:
  void DidChangeTargetState(Target* target, Target::State old_state) override;

  // ProcessObserver implementation:
  void DidCreateThread(Process* process, Thread* thread) override;
  void WillDestroyThread(Process* process, Thread* thread) override;

  // ThreadObserver implementation:
  void OnThreadStopped(Thread* thread) override;

  // Returns the record for the given target, or null (+ assertion) if not
  // found. These pointers are not stable across target list changes.
  TargetRecord* GetTargetRecord(int target_id);
  TargetRecord* GetTargetRecord(Target* target);

  Session* const session_;

  // The ID from a user perspective maps to a Target/Process pair.
  std::map<int, TargetRecord> id_to_target_;
  std::map<Target*, int> target_to_id_;
  int next_target_id_ = 1;

  int active_target_id_ = 0;
};

}  // namespace zxdb

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "garnet/bin/zxdb/console/command.h"

namespace zxdb {

class Breakpoint;
class Command;
class Frame;
class Session;

// The context for console commands. In a model-view-controller UI, this would
// represent the state associated with the view and controller (depending on
// how one splits things up). It keeps track of the currently selected
// objects and watches for changes.
//
// This class maintains the mapping between objects and IDs.
class ConsoleContext : public ProcessObserver,
                       public SystemObserver,
                       public TargetObserver,
                       public ThreadObserver,
                       public BreakpointObserver {
 public:
  explicit ConsoleContext(Session* session);
  ~ConsoleContext();

  Session* session() { return session_; }

  // Returns the ID for the object. Asserts and returns 0 if not found.
  int IdForTarget(const Target* target) const;
  int IdForThread(const Thread* thread) const;
  int IdForFrame(const Frame* frame) const;
  int IdForBreakpoint(const Breakpoint* breakpoint) const;

  // The active target will always exist except during setup and teardown.
  void SetActiveTarget(const Target* target);
  int GetActiveTargetId() const;
  Target* GetActiveTarget() const;

  // The active thread for its target. The active target is not affected. The
  // active thread ID for a target not running will be 0.
  void SetActiveThreadForTarget(const Thread* thread);
  int GetActiveThreadIdForTarget(const Target* target);

  // Frames are a little bit different than threads and targets since they
  // have an intrinsic numbering supplied by the Thread object (the index into
  // the backtrace). If there are no frames on the thread, the return value
  // will be 0 (so the return value can't be blindly indexed into the frames
  // list).
  void SetActiveFrameForThread(const Frame* frame);
  int GetActiveFrameIdForThread(const Thread* thread);

  // Sets the active breakpoint. Can be null/0 if there is no active breakpoint
  // (set to null to clear).
  void SetActiveBreakpoint(const Breakpoint* breakpoint);
  int GetActiveBreakpointId() const;
  Breakpoint* GetActiveBreakpoint() const;

  // Each thread maintains a source affinity which was the last command that
  // implies either source code or disassembly viewing. This is used to control
  // what gets displayed by default for the next stop of that thread. Defaults
  // to kSource for new and unknown threads. Setting SourceAffinity::kNone does
  // nothing so calling code can unconditionally call for all commands.
  SourceAffinity GetSourceAffinityForThread(const Thread* thread) const;
  void SetSourceAffinityForThread(const Thread* thread,
                                  SourceAffinity source_affinity);

  // Fills the current effective process, thread, etc. into the given Command
  // structure based on what the command specifies and the current context.
  // Returns an error if any of the referenced IDs are invalid.
  Err FillOutCommand(Command* cmd) const;

 private:
  struct ThreadRecord {
    Thread* thread = nullptr;
    int active_frame_id = 0;

    // Default to showing source code for thread stops.
    SourceAffinity source_affinity = SourceAffinity::kSource;
  };

  struct TargetRecord {
    int target_id = 0;
    Target* target = nullptr;

    int next_thread_id = 1;

    // The active ID will be 0 when there is no active thread (the case when
    // the process is not running).
    int active_thread_id = 0;

    std::map<int, ThreadRecord> id_to_thread;
    std::map<const Thread*, int> thread_to_id;
  };

  // SystemObserver implementation:
  void DidCreateTarget(Target* target) override;
  void WillDestroyTarget(Target* target) override;
  void DidCreateBreakpoint(Breakpoint* breakpoint) override;
  void WillDestroyBreakpoint(Breakpoint* breakpoint) override;
  void DidTryToLoadSymbolMapping(bool ids_loaded,
                                 const std::string& msg) override;

  // TargetObserver implementation:
  void DidCreateProcess(Target* target, Process* process) override;
  void WillDestroyProcess(Target* target, Process* process,
                          DestroyReason reason, int exit_code) override;

  // ProcessObserver implementation:
  void DidCreateThread(Process* process, Thread* thread) override;
  void WillDestroyThread(Process* process, Thread* thread) override;
  void OnSymbolLoadFailure(Process* process, const Err& err) override;

  // ThreadObserver implementation:
  void OnThreadStopped(
      Thread* thread, debug_ipc::NotifyException::Type type,
      std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) override;
  void OnThreadFramesInvalidated(Thread* thread) override;

  // Returns the record for the given target, or null (+ assertion) if not
  // found. These pointers are not stable across target list changes.
  TargetRecord* GetTargetRecord(int target_id);
  const TargetRecord* GetTargetRecord(int target_id) const;
  TargetRecord* GetTargetRecord(const Target* target);
  const TargetRecord* GetTargetRecord(const Target* target) const;

  ThreadRecord* GetThreadRecord(const Thread* thread);
  const ThreadRecord* GetThreadRecord(const Thread* thread) const;

  // Backends for parts of FillOutCommand.
  //
  // For the variants that take an input pointer, the pointer may be null if
  // there is nothing of that type.
  //
  // For the variants that take an output pointer, the pointer will be stored
  // if the corresponding item (target/thread) is found, otherwise it will be
  // unchanged.
  Err FillOutTarget(Command* cmd, TargetRecord const** out_target_record) const;
  Err FillOutThread(Command* cmd, const TargetRecord* target_record,
                    ThreadRecord const** out_thread_record) const;
  Err FillOutFrame(Command* cmd, const ThreadRecord* thread_record) const;
  Err FillOutBreakpoint(Command* cmd) const;

  // Generates a string describing the breakpoints that were hit.
  std::string DescribeHitBreakpoints(
      const std::vector<fxl::WeakPtr<Breakpoint>>& hits) const;

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

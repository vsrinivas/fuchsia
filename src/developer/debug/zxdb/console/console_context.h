// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_CONTEXT_H_

#include "src/developer/debug/zxdb/client/breakpoint_observer.h"
#include "src/developer/debug/zxdb/client/download_observer.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/session_observer.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/console/command.h"

namespace zxdb {

class Breakpoint;
class Command;
class Filter;
class Frame;
class Session;

// The context for console commands. In a model-view-controller UI, this would
// represent the state associated with the view and controller (depending on
// how one splits things up). It keeps track of the currently selected
// objects and watches for changes.
//
// This class maintains the mapping between objects and IDs.
class ConsoleContext : public ProcessObserver,
                       public SessionObserver,
                       public SystemObserver,
                       public TargetObserver,
                       public ThreadObserver,
                       public BreakpointObserver,
                       public DownloadObserver {
 public:
  explicit ConsoleContext(Session* session);
  ~ConsoleContext();

  Session* session() { return session_; }

  // Returns the ID for the object. Asserts and returns 0 if not found.
  int IdForTarget(const Target* target) const;
  int IdForJobContext(const JobContext* job_context) const;
  int IdForThread(const Thread* thread) const;
  int IdForFrame(const Frame* frame) const;
  int IdForBreakpoint(const Breakpoint* breakpoint) const;
  int IdForFilter(const Filter* filter) const;
  int IdForSymbolServer(const SymbolServer* symbol_server) const;

  // The active target will always exist except during setup and teardown.
  void SetActiveTarget(const Target* target);
  int GetActiveTargetId() const;
  Target* GetActiveTarget() const;

  // The active symbol server may or may not exist.
  void SetActiveSymbolServer(const SymbolServer* target);
  int GetActiveSymbolServerId() const;
  SymbolServer* GetActiveSymbolServer() const;

  // The active job context will always exist except during setup and teardown.
  void SetActiveJobContext(const JobContext* job_context);
  int GetActiveJobContextId() const;
  JobContext* GetActiveJobContext() const;

  // The active thread for its target. The active target is not affected. The
  // active thread ID for a target not running will be 0.
  void SetActiveThreadForTarget(const Thread* thread);
  int GetActiveThreadIdForTarget(const Target* target);
  Thread* GetActiveThreadForTarget(const Target* target);

  // Frames are a little bit different than threads and targets since they
  // have an intrinsic numbering supplied by the Thread object (the index into
  // the backtrace). If there are no frames on the thread, the return value
  // will be 0 (so the return value can't be blindly indexed into the frames
  // list).
  void SetActiveFrameForThread(const Frame* frame);
  void SetActiveFrameIdForThread(const Thread* thread, int id);
  int GetActiveFrameIdForThread(const Thread* thread) const;

  // Sets the active breakpoint. Can be null/0 if there is no active breakpoint
  // (set to null to clear).
  void SetActiveBreakpoint(const Breakpoint* breakpoint);
  int GetActiveBreakpointId() const;
  Breakpoint* GetActiveBreakpoint() const;

  // Sets the active filter. Can be null/0 if there is no active filter (set to
  // null to clear).
  void SetActiveFilter(const Filter* filter);
  int GetActiveFilterId() const;
  Filter* GetActiveFilter() const;

  // Each thread maintains a source affinity which was the last command that
  // implies either source code or disassembly viewing. This is used to control
  // what gets displayed by default for the next stop of that thread. Defaults
  // to kSource for new and unknown threads. Setting SourceAffinity::kNone does
  // nothing so calling code can unconditionally call for all commands.
  SourceAffinity GetSourceAffinityForThread(const Thread* thread) const;
  void SetSourceAffinityForThread(const Thread* thread, SourceAffinity source_affinity);

  // Outputs to the console information on the given stopped thread with the
  // given reasons for stopping.
  void OutputThreadContext(const Thread* thread, debug_ipc::ExceptionType type,
                           const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) const;

  // Fills the current effective process, thread, etc. into the given Command
  // structure based on what the command specifies and the current context.
  // Returns an error if any of the referenced IDs are invalid.
  Err FillOutCommand(Command* cmd) const;

 private:
  struct ThreadRecord {
    Thread* thread = nullptr;

    // This isn't necessarily valid since the frames could have been changed
    // out from under us. Be sure to range check before use.
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

  struct JobContextRecord {
    int job_context_id = 0;
    JobContext* job_context = nullptr;
  };

  // SessionObserver implementation:
  void HandleNotification(NotificationType, const std::string&) override;
  void HandlePreviousConnectedProcesses(const std::vector<debug_ipc::ProcessRecord>&) override;
  void HandleProcessesInLimbo(const std::vector<debug_ipc::ProcessRecord>&) override;

  // SystemObserver implementation:
  void DidCreateJobContext(JobContext* job_context) override;
  void DidCreateBreakpoint(Breakpoint* breakpoint) override;
  void WillDestroyBreakpoint(Breakpoint* breakpoint) override;
  void DidCreateFilter(Filter* filter) override;
  void DidCreateSymbolServer(SymbolServer* symbol_server) override;
  void OnSymbolIndexingInformation(const std::string& msg) override;

  // TargetObserver implementation:
  void DidCreateTarget(Target* target) override;
  void WillDestroyTarget(Target* target) override;

  // ProcessObserver implementation:
  void DidCreateProcess(Process* process, bool autoattached_to_new_process) override;
  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code) override;
  void OnSymbolLoadFailure(Process* process, const Err& err) override;

  // ThreadObserver implementation:
  void DidCreateThread(Thread* thread) override;
  void WillDestroyThread(Thread* thread) override;
  void OnThreadStopped(Thread* thread, debug_ipc::ExceptionType type,
                       const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override;
  void OnThreadFramesInvalidated(Thread* thread) override;

  // DownloadObserver implementation:
  void OnDownloadsStarted() override;
  void OnDownloadsStopped(size_t success, size_t fail) override;

  // BreakpointObserver implementation.
  void OnBreakpointMatched(Breakpoint* breakpoint, bool user_requested) override;

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
  Err FillOutJobContext(Command* cmd) const;
  Err FillOutThread(Command* cmd, const TargetRecord* target_record,
                    ThreadRecord const** out_thread_record) const;
  Err FillOutFrame(Command* cmd, const ThreadRecord* thread_record) const;
  Err FillOutBreakpoint(Command* cmd) const;
  Err FillOutFilter(Command* cmd) const;
  Err FillOutSymbolServer(Command* cmd) const;

  // Generates a string describing the breakpoints that were hit.
  std::string DescribeHitBreakpoints(const std::vector<fxl::WeakPtr<Breakpoint>>& hits) const;

  Session* const session_;

  // The ID from a user perspective maps to a Target/Process pair.
  std::map<int, TargetRecord> id_to_target_;
  std::map<const Target*, int> target_to_id_;
  int next_target_id_ = 1;

  std::map<int, JobContextRecord> id_to_job_context_;
  std::map<const JobContext*, int> job_context_to_id_;
  int next_job_context_id_ = 1;

  std::map<int, Breakpoint*> id_to_breakpoint_;
  std::map<const Breakpoint*, int> breakpoint_to_id_;
  int next_breakpoint_id_ = 1;

  std::map<int, Filter*> id_to_filter_;
  std::map<const Filter*, int> filter_to_id_;
  int next_filter_id_ = 1;

  std::map<int, SymbolServer*> id_to_symbol_server_;
  std::map<const SymbolServer*, int> symbol_server_to_id_;
  int next_symbol_server_id_ = 1;

  int active_target_id_ = 0;
  int active_job_context_id_ = 0;
  int active_breakpoint_id_ = 0;
  int active_filter_id_ = 0;
  int active_symbol_server_id_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_CONTEXT_H_

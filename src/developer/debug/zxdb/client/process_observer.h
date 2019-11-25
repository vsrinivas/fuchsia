// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_OBSERVER_H_

namespace zxdb {

class Err;
class LoadedModuleSymbols;
class Process;
class Thread;

// Note: process creation and destruction events are on the TargetObserver.
class ProcessObserver {
 public:
  // Called immediately after creating a new thread or attaching to the process.
  virtual void DidCreateThread(Process* process, Thread* thread) {}

  // Called immediately before destroying an existing thread.
  virtual void WillDestroyThread(Process* process, Thread* thread) {}

  // Notification that a module with symbols is ready to use.
  //
  // Note: There is currently no notification for module loads absent symbol information. If that's
  // necessary, this will need refactoring.
  virtual void DidLoadModuleSymbols(Process* process, LoadedModuleSymbols* module) {}

  // Notification that the given module with symbols is about to be removed.
  virtual void WillUnloadModuleSymbols(Process* process, LoadedModuleSymbols* module) {}

  // Called when symbols for a loaded binary could not be loaded.
  virtual void OnSymbolLoadFailure(Process* process, const Err& err) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_OBSERVER_H_

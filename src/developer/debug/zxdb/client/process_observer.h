// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_OBSERVER_H_

#include <stdint.h>

namespace zxdb {

class Err;
class LoadedModuleSymbols;
class Process;
class Thread;

class ProcessObserver {
 public:
  // Reason for destroying a process object.
  enum class DestroyReason { kExit, kDetach, kKill };
  static const char* DestroyReasonToString(DestroyReason);

  // Called after a process is created.
  virtual void DidCreateProcess(Process* process, uint64_t timestamp) {}

  // Called after detaching from or destroying a process. The Process object will still exist on the
  // Target but the Target will report |state == kNone|.
  //
  // The exit code will only have meaning when reason == kExit, otherwise it will be 0.
  virtual void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                                  uint64_t timestamp) {}

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

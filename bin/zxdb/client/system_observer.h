// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class Breakpoint;
class Process;
class Target;

class SystemObserver {
 public:
  // Called immediately after creation / before destruction of a target.
  virtual void DidCreateTarget(Target* target) {}
  virtual void WillDestroyTarget(Target* target) {}

  // It can be common to want to watch for all Process creation and destruction
  // events. For convenience, these allow that without having to track each
  // Target and register as an observer on them individually.
  virtual void GlobalDidCreateProcess(Process* process) {}
  virtual void GlobalWillDestroyProcess(Process* process) {}

  // Called immediately after creation / before destruction of a breakpoint.
  virtual void DidCreateBreakpoint(Breakpoint* breakpoint) {}
  virtual void WillDestroyBreakpoint(Breakpoint* breakpoint) {}

  // Indicates an informational message from the symbol indexing system.
  // This will be things like "X" symbols loaded from "Y".
  virtual void OnSymbolIndexingInformation(const std::string& msg) {}
};

}  // namespace zxdb

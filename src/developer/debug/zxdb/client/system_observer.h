// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_OBSERVER_H_

#include <string>

namespace zxdb {

class Breakpoint;
class Filter;
class Process;
class Target;
class Job;
class SymbolServer;

class SystemObserver {
 public:
  // Called immediately after creation / before destruction of a job.
  virtual void DidCreateJob(Job* job) {}
  virtual void WillDestroyJob(Job* job) {}

  // Called immediately after createion of a symbol server.
  virtual void DidCreateSymbolServer(SymbolServer* server) {}

  // Called immediately after the status of a symbol server changes.
  virtual void OnSymbolServerStatusChanged(SymbolServer* server) {}

  // Called immediately after creation / before destruction of a breakpoint.
  virtual void DidCreateBreakpoint(Breakpoint* breakpoint) {}
  virtual void WillDestroyBreakpoint(Breakpoint* breakpoint) {}

  // Called immediately after creation / before destruction of a filter.
  virtual void DidCreateFilter(Filter* filter) {}
  virtual void WillDestroyFilter(Filter* filter) {}

  // Indicates an informational message from the symbol indexing system. This will be things like
  // "X" symbols loaded from "Y".
  virtual void OnSymbolIndexingInformation(const std::string& msg) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_OBSERVER_H_

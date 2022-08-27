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
class SymbolServer;

class SystemObserver {
 public:
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
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_OBSERVER_H_

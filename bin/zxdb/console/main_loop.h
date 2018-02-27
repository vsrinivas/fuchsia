// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class AgentConnection;

// The main loop for the console. This reads text commands from stdin and
// manages notification of the AgentConnection when the connection is
// readable.
//
// This mashup of AgentConnection notifications and stdio is appropriate for
// the command-line debugger, but may need to be separated out or generalized
// to support non-commandline-based implementations.
class MainLoop {
 public:
  MainLoop();
  virtual ~MainLoop();

  void Run();

  // The AgentConnection is registered with the MainLoop to begin getting
  // notifications about the native handle being readable and writable.
  //
  // The AgentConnection must stop watching before it goes out of scope.
  //
  // StartWatchingConnection will immediately trigger an OnNativeHandleReadable
  // call to kick off the connection (since the watcher is edge-triggered only,
  // any existing data needs to be read before any notifications will come from
  // the OS).
  void StartWatchingConnection(AgentConnection* connection);
  void StopWatchingConnection(AgentConnection* connection);

 protected:
  // To be implemented by the platform-specific derived classes.
  //
  // The implementation should check should_quit() after dispatching any
  // operation and exit if true.
  //
  // It should dispatch data from the agent connection to OnAgentData(), and
  // call OnStdinReadable() when stdin transitions to a readable state.
  virtual void PlatformRun() = 0;

  // Platform-specific versions of the above functions. These do not need to do
  // any bookeeping on the connections, only register or unregister the
  // connection handle.
  //
  // The connection_id is to be used in the future to look up the connection
  // via ConnectionFromID(). It will not be 0.
  virtual void PlatformStartWatchingConnection(
      size_t connection_id, AgentConnection* connection) = 0;
  virtual void PlatformStopWatchingConnection(
      size_t connection_id, AgentConnection* connection) = 0;

  void set_should_quit() { should_quit_ = true; }
  bool should_quit() const { return should_quit_; }

  // Called by the platform-specific derived classes when stdin transitions to
  // a readable state.
  void OnStdinReadable();

  // Returns the connection associated with the given ID (provided to
  // PlatformStartWatchingConnection). Returns nullptr if not found.
  AgentConnection* ConnectionFromID(size_t connection_id);

 private:
  // Registered connections that this class is watching. Non-owning pointers.
  //
  // Connections are assigned increasing IDs that can be used to map back to
  // the pointers. We don't actually expect to have more than one connection
  // at a time. But we do need to support changing over time between no
  // connection and different connections, and it's nice to have some sanity
  // checking on handle watching that the incoming events are for the
  // connection we expect, and using a map + unique IDs solves this problem.
  std::map<size_t, AgentConnection*> connections_;
  size_t next_connection_id_ = 1;

  bool should_quit_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MainLoop);
};

}  // namespace zxdb

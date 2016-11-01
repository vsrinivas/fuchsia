// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_PLATFORM_DART_DART_HANDLE_WATCHER_H_
#define MOJO_PUBLIC_PLATFORM_DART_DART_HANDLE_WATCHER_H_

#include <mojo/system/handle.h>
#include <mojo/system/result.h>

#include <mutex>
#include <thread>
#include <unordered_map>

#include "dart/runtime/include/dart_api.h"

#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"

namespace mojo {
namespace dart {

#define MOJO_HANDLE_SIGNAL_ALL (MOJO_HANDLE_SIGNAL_READABLE |                  \
                                MOJO_HANDLE_SIGNAL_WRITABLE |                  \
                                MOJO_HANDLE_SIGNAL_PEER_CLOSED)

// HandleWatcherCommands are sent to HandleWatchers.
class HandleWatcherCommand {
 public:
  enum Command {
    kCommandAddHandle = 0,
    kCommandRemoveHandle = 1,
    kCommandCloseHandle = 2,
    kCommandAddTimer = 3,
    kCommandShutdownHandleWatcher = 4,
  };

  // Construct a command to listen for |handle| to have |signals| and ping
  // |port| when this happens.
  static HandleWatcherCommand Add(MojoHandle handle,
                                  MojoHandleSignals signals,
                                  Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<int64_t>(handle);
    result.port_ = port;
    result.set_data(kCommandAddHandle, signals);
    return result;
  }

  // Construct a command to stop listening for |handle|.
  static HandleWatcherCommand Remove(MojoHandle handle) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<int64_t>(handle);
    result.port_ = ILLEGAL_PORT;
    result.set_data(kCommandRemoveHandle, MOJO_HANDLE_SIGNAL_NONE);
    return result;
  }

  // Construct a command to close |handle| and ping |port| when done.
  static HandleWatcherCommand Close(MojoHandle handle,
                                    Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<int64_t>(handle);
    result.port_ = port;
    result.set_data(kCommandCloseHandle, MOJO_HANDLE_SIGNAL_NONE);
    return result;
  }

  // Construct a command to ping |port| when it is |deadline|.
  static HandleWatcherCommand Timer(int64_t deadline,
                                    Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = deadline;
    result.port_ = port;
    result.set_data(kCommandAddTimer, MOJO_HANDLE_SIGNAL_NONE);
    return result;
  }

  // Construct a command to shutdown the handle watcher thread.
  static HandleWatcherCommand Shutdown() {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = MOJO_HANDLE_INVALID;
    result.port_ = ILLEGAL_PORT;
    result.set_data(kCommandShutdownHandleWatcher, MOJO_HANDLE_SIGNAL_NONE);
    return result;
  }

  // Construct an empty command.
  static HandleWatcherCommand Empty() {
    HandleWatcherCommand result;
    return result;
  }

  // Construct a command sent from Dart code.
  static HandleWatcherCommand FromDart(int64_t command,
                                       int64_t handle_or_deadline,
                                       Dart_Port port,
                                       int64_t signals) {
    switch (command) {
      case kCommandAddHandle:
        return Add(handle_or_deadline, signals, port);
      break;
      case kCommandRemoveHandle:
        return Remove(handle_or_deadline);
      break;
      case kCommandCloseHandle:
        return Close(handle_or_deadline, port);
      break;
      case kCommandAddTimer:
        return Timer(handle_or_deadline, port);
      break;
      case kCommandShutdownHandleWatcher:
        return Shutdown();
      break;
      default:
        // Unreachable.
        MOJO_CHECK(false);
        return Empty();
    }
  }

  // Get the command.
  Command command() const {
    return static_cast<Command>((data_ >> 3));
  }

  // Get the signals associated with the command.
  MojoHandleSignals signals() const {
    return data_ & MOJO_HANDLE_SIGNAL_ALL;
  }

  // Get the handle associated with the command.
  MojoHandle handle() const {
    return static_cast<MojoHandle>(handle_or_deadline_);
  }

  // Get the deadline associated with the command.
  int64_t deadline() const {
    return handle_or_deadline_;
  }

  // Get the port associated with the command.
  Dart_Port port() const {
    return port_;
  }

 private:
  HandleWatcherCommand() {
    handle_or_deadline_ = 0;
    port_ = ILLEGAL_PORT;
    data_ = 0;
  }

  void set_data(Command command, MojoHandleSignals signals) {
    MOJO_CHECK(MOJO_HANDLE_SIGNAL_ALL < (1 << 3));
    data_ = (command << 3) | (signals & MOJO_HANDLE_SIGNAL_ALL);
  }

  int64_t handle_or_deadline_;
  Dart_Port port_;
  int64_t data_;
};


// A Dart HandleWatcher can be started by calling |HandleWatcher::Start|.
// Each |Start| call creates a message pipe for communicating with the
// handle watcher and spawns a thread where the handle watcher waits for
// events on handles.
//
// NOTE: If multiple handle watchers are needed, |Start| can be safely called
// multiple times because all state is held inside the spawned thread.
class HandleWatcher {
 public:
  // Starts a new HandleWatcher thread and returns the message pipe handle
  // that is used to communicate with the handle watcher. Returns
  // the handle that should be passed to |SendCommand|.
  static MojoHandle Start();

  // Encode a |command| for the handle watcher and write it to
  // |control_pipe_producer_handle|.
  static MojoResult SendCommand(MojoHandle control_pipe_producer_handle,
                                const HandleWatcherCommand& command);

  // Stops and joins the handle watcher thread at the other end of the
  // given pipe handle.
  static void Stop(MojoHandle control_pipe_consumer_handle);

  // Stops and joins all handle watcher threads.
  static void StopAll();

 private:
  static void ThreadMain(MojoHandle control_pipe_consumer_handle);

  // Remove the mapping for |handle| from |handle_watcher_threads_| and return
  // the associated thread object. Assumes |handle_watcher_threads_mutex_| is
  // held.
  static std::thread* RemoveLocked(MojoHandle handle);

  // Remove the mapping for |handle| from |handle_watcher_threads_| and join
  // the associated thread. Assumes |handle_watcher_threads_mutex_| is held.
  static void StopLocked(MojoHandle handle);

  // A mapping from control handle to handle watcher thread.
  static std::unordered_map<MojoHandle, std::thread*> handle_watcher_threads_;

  // Protects |handle_watcher_threads_|
  static std::mutex handle_watcher_threads_mutex_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};


}  // namespace dart
}  // namespace mojo

#endif  // MOJO_PUBLIC_PLATFORM_DART_DART_HANDLE_WATCHER_H_

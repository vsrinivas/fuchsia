// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WATCHER_H_
#define LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WATCHER_H_

#include <magenta/syscalls.h>
#include <mx/channel.h>

#include <mutex>
#include <thread>
#include <unordered_map>

#include "dart/runtime/include/dart_api.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace fidl {
namespace dart {

#define FIDL_DART_HANDLE_SIGNAL_ALL \
  (MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED)

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
  static HandleWatcherCommand Add(mx_handle_t handle,
                                  mx_signals_t signals,
                                  Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<uint64_t>(handle);
    result.port_ = port;
    result.set_data(kCommandAddHandle, signals);
    return result;
  }

  // Construct a command to stop listening for |handle|.
  static HandleWatcherCommand Remove(mx_handle_t handle) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<uint64_t>(handle);
    result.port_ = ILLEGAL_PORT;
    result.set_data(kCommandRemoveHandle, MX_SIGNAL_NONE);
    return result;
  }

  // Construct a command to close |handle| and ping |port| when done.
  static HandleWatcherCommand Close(mx_handle_t handle, Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<uint64_t>(handle);
    result.port_ = port;
    result.set_data(kCommandCloseHandle, MX_SIGNAL_NONE);
    return result;
  }

  // Construct a command to ping |port| when it is |deadline|.
  static HandleWatcherCommand Timer(mx_time_t deadline, Dart_Port port) {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = deadline;
    result.port_ = port;
    result.set_data(kCommandAddTimer, MX_SIGNAL_NONE);
    return result;
  }

  // Construct a command to shutdown the handle watcher thread.
  static HandleWatcherCommand Shutdown() {
    HandleWatcherCommand result;
    result.handle_or_deadline_ = static_cast<uint64_t>(MX_HANDLE_INVALID);
    result.port_ = ILLEGAL_PORT;
    result.set_data(kCommandShutdownHandleWatcher, MX_SIGNAL_NONE);
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
        FTL_CHECK(false);
        return Empty();
    }
  }

  // Get the command.
  Command command() const { return static_cast<Command>((data_ >> 3)); }

  // Get the signals associated with the command.
  mx_signals_t signals() const { return data_ & FIDL_DART_HANDLE_SIGNAL_ALL; }

  // Get the handle associated with the command.
  mx_handle_t handle() const {
    return static_cast<mx_handle_t>(handle_or_deadline_);
  }

  // Get the deadline associated with the command.
  int64_t deadline() const { return handle_or_deadline_; }

  // Get the port associated with the command.
  Dart_Port port() const { return port_; }

 private:
  HandleWatcherCommand() {
    handle_or_deadline_ = 0;
    port_ = ILLEGAL_PORT;
    data_ = 0;
  }

  void set_data(Command command, mx_signals_t signals) {
    FTL_CHECK(FIDL_DART_HANDLE_SIGNAL_ALL < (1 << 3));
    data_ = (command << 3) | (signals & FIDL_DART_HANDLE_SIGNAL_ALL);
  }

  uint64_t handle_or_deadline_;
  Dart_Port port_;
  int64_t data_;
};

// A Dart HandleWatcher can be started by calling |HandleWatcher::Start|.
// Each |Start| call creates a channel for communicating with the handle watcher
// and spawns a thread where the handle watcher waits for events on handles.
//
// NOTE: If multiple handle watchers are needed, |Start| can be safely called
// multiple times because all state is held inside the spawned thread.
class HandleWatcher {
 public:
  // Starts a new HandleWatcher thread and returns the channel that is used to
  // communicate with the handle watcher. Returns the channel whose handle
  // should be passed to |SendCommand|.
  static mx::channel Start();

  // Encode a |command| for the handle watcher and write it to
  // |producer_handle|.
  static mx_status_t SendCommand(mx_handle_t producer_handle,
                                 const HandleWatcherCommand& command);

  // Stops and joins the handle watcher thread at the other end of the
  // given channel handle.
  static void Stop(mx::channel producer_handle);

  // Stops and joins all handle watcher threads.
  static void StopAll();

 private:
  static void ThreadMain(mx_handle_t consumer_handle);

  // Remove the mapping for |producer_handle| from |handle_watcher_threads_| and
  // return the associated thread object. Assumes
  // |handle_watcher_threads_mutex_| is held.
  static std::thread RemoveLocked(mx_handle_t producer_handle);

  // Remove the mapping for |producer_handle| from |handle_watcher_threads_| and
  // join the associated thread. Assumes |handle_watcher_threads_mutex_| is
  // held.
  static void StopLocked(mx_handle_t producer_handle);

  // A mapping from control handle to handle watcher thread.
  static std::unordered_map<mx_handle_t, std::thread> handle_watcher_threads_;

  // Protects |handle_watcher_threads_|
  static std::mutex handle_watcher_threads_mutex_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HandleWatcher);
};

}  // namespace dart
}  // namespace fidl

#endif  // LIB_FIDL_DART_SDK_EXT_SRC_HANDLE_WATCHER_H_

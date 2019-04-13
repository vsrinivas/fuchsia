// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/unsafe.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>

#include "src/developer/debug/shared/message_loop_target.h"

namespace debug_ipc {

class ZirconExceptionWatcher;
class SocketWatcher;

class MessageLoopZircon : public MessageLoopTarget {
 public:
  MessageLoopZircon();
  ~MessageLoopZircon();

  void Init() override;
  zx_status_t InitTarget() override;

  void Cleanup() override;
  void QuitNow() override;

  Type GetType() const override { return Type::kZircon; }
  bool SupportsFidl() const override { return false; }

  // Returns the current message loop or null if there isn't one.
  static MessageLoopZircon* Current();

  // MessageLoop implementation.
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) override;

  zx_status_t WatchSocket(WatchMode mode, zx_handle_t socket_handle,
                          SocketWatcher* watcher, WatchHandle* out) override;

  zx_status_t WatchProcessExceptions(WatchProcessConfig config,
                                     WatchHandle* out) override;

  zx_status_t WatchJobExceptions(WatchJobConfig config,
                                 WatchHandle* out) override;

  zx_status_t ResumeFromException(zx_koid_t thread_koid, zx::thread& thread,
                                  uint32_t options) override;

 private:
  // Associated struct to track information about what type of resource a watch
  // handle is following.
  struct WatchInfo;

  // MessageLoop protected implementation.
  uint64_t GetMonotonicNowNS() const override;
  void RunImpl() override;
  void StopWatching(int id) override;
  // Triggers an event signaling that there is a pending event.
  void SetHasTasks() override;

  // Check for any pending C++ tasks and process them.
  // Returns true if there was an event pending to be processed.
  bool CheckAndProcessPendingTasks();

  // Handles WatchHandles event. These are all the events that are not C++
  // tasks posted to the message loop.
  void HandleException(zx_port_packet_t packet);

  // Handle an event of the given type.
  void OnFdioSignal(int watch_id, const WatchInfo& info,
                    const zx_port_packet_t& packet);
  void OnProcessException(const WatchInfo& info,
                          const zx_port_packet_t& packet);
  void OnJobException(const WatchInfo& info, const zx_port_packet_t& packet);
  void OnSocketSignal(int watch_id, const WatchInfo& info,
                      const zx_port_packet_t& packet);

  using WatchMap = std::map<int, WatchInfo>;
  WatchMap watches_;

  // ID used as an index into watches_.
  int next_watch_id_ = 1;

  zx::port port_;

  // This event is signaled when there are tasks to process.
  zx::event task_event_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoopZircon);
};

}  // namespace debug_ipc

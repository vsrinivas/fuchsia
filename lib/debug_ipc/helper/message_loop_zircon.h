// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop.h"

#include <zx/event.h>
#include <zx/port.h>

namespace debug_ipc {

class ZirconExceptionWatcher;
class SocketWatcher;

class MessageLoopZircon : public MessageLoop {
 public:
  MessageLoopZircon();
  ~MessageLoopZircon();

  void Init() override;
  void Cleanup() override;

  // Returns the current message loop or null if there isn't one.
  static MessageLoopZircon* Current();

  // MessageLoop implementation.
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) override;

  // Watches the given socket for read/write status. The watcher must outlive
  // the returned WatchHandle. Must only be called on the message loop thread.
  //
  // The FDWatcher must not unregister from a callback. The handle might
  // become both readable and writable at the same time which will necessitate
  // calling both callbacks. The code does not expect the FDWatcher to
  // disappear in between these callbacks.
  WatchHandle WatchSocket(WatchMode mode, zx_handle_t socket_handle,
                          SocketWatcher* watcher);

  // Attaches to the exception port of the given process and issues callbacks
  // on the given watcher. The watcher must outlive the returned WatchHandle.
  // Must only be called on the message loop thread.
  WatchHandle WatchProcessExceptions(zx_handle_t process_handle,
                                     zx_koid_t process_koid,
                                     ZirconExceptionWatcher* watcher);

  // When this class issues an exception notification, the code should call
  // this function to resume the thread from the exception. This is a wrapper
  // for zx_task_resume_from_exception.
  zx_status_t ResumeFromException(zx_handle_t thread_handle, uint32_t options);

 private:
  enum class WatchType { kFdio, kProcessExceptions, kSocket };
  struct WatchInfo;

  // MessageLoop protected implementation.
  void RunImpl() override;
  void StopWatching(int id) override;
  void SetHasTasks() override;

  // Handle an event of the given type.
  void OnFdioSignal(int watch_id, const WatchInfo& info,
                    const zx_port_packet_t& packet);
  void OnProcessException(const WatchInfo& info,
                          const zx_port_packet_t& packet);
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

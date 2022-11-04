// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_WAKEUP_EVENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_WAKEUP_EVENT_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>

namespace media::audio {

// WakeupEvent is used to implement a style of auto-reset event based on a zircon event object.
class WakeupEvent {
 public:
  static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;

  WakeupEvent();

  // WakeupEvent defines a single handler (ProcessHandler) which runs when the event becomes
  // signaled at least once. Returning an error from the process handler will cause the event to
  // automatically become deactivated.
  using ProcessHandler = fit::inline_function<zx_status_t(WakeupEvent*), MAX_HANDLER_CAPTURE_SIZE>;

  // Activation simply requires a user to provide a valid async dispatcher and a valid
  // ProcessHandler. The event handle itself will be allocated internally.
  //
  // Requires that |dispatcher| is a single-threaded dispatcher and this method is called on that
  // dispatch thread.
  zx_status_t Activate(async_dispatcher_t* dispatcher, ProcessHandler process_handler);

  // Activation simply requires a user to provide a valid async dispatcher and a valid
  // ProcessHandler. The event handle itself will be allocated internally.
  //
  // Requires that the WakeupEvent was previously Activated with a single-threaded dispatcher and
  // that this method is called on the dispatch thread.
  zx_status_t Deactivate();

  // Signaling a WakupEvent to fire is an operation that may be called from any thread. Signaling a
  // WakeupEvent multiple times before it gets dispatched will result in only a single dispatch
  // event. A WakeupEvent becomes un-signaled just before the registered ProcessHandler is called;
  // it may become resignaled during the dispatch operation itself resulting in another call to the
  // ProcessHandler (provided that the event does not become Deactivated).
  zx_status_t Signal() const;

 private:
  void OnSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                 const zx_packet_signal_t* signal);

  zx::event event_;
  ProcessHandler process_handler_;
  async::WaitMethod<WakeupEvent, &WakeupEvent::OnSignals> wait_{this};
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_WAKEUP_EVENT_H_

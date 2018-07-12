// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_EVENT_TIMESTAMPER_H_
#define GARNET_LIB_UI_GFX_UTIL_EVENT_TIMESTAMPER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace scenic {
namespace gfx {

// EventTimestamper uses a background thread to watch for signals specified
// by EventTimestamper::Watch objects.  When a signal is observed, a task is
// posted on the main loop to invoke a callback that is provided by the client.
//
// A program typically needs/wants a single EventTimestamper, which is shared
// by everyone who needs event-timestamps.
class EventTimestamper {
 private:
  class Waiter;

 public:
  using Callback = fit::function<void(zx_time_t timestamp)>;

  EventTimestamper();
  ~EventTimestamper();

  // When the Start() method is called, a Watch object begins to watch its event
  // for the specified trigger signal.  When the event occurs, the callback will
  // be invoked, once.  To watch for subsequent signals, Start() must be called
  // again.  It is illegal to call Start() again before the previous callback
  // has been received.  It is safe to destroy the Watch object even if Start()
  // has been called; in this case, it is guaranteed that the callback will not
  // be invoked.
  class Watch {
   public:
    Watch();
    Watch(EventTimestamper* ts, zx::event event, zx_status_t trigger,
          Callback callback);
    Watch(Watch&& rhs);
    Watch& operator=(Watch&& rhs);
    ~Watch();

    // Start watching for the event to be signaled.  It is illegal to call
    // Start() again before the callback has been invoked (it is safe to invoke
    // Start() again from within the callback).
    void Start();

    // Return the watched event (or a null handle, if this Watch was moved).
    const zx::event& event() const;

   private:
    Waiter* waiter_;
    EventTimestamper* timestamper_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Watch);
  };

 private:
  // Helper object that stores state corresponding to a single Watch object.
  // Invariants:
  // - |state_| only changes on the main thread.
  // - instances of Waiter are only destroyed on the main thread.
  class Waiter {
   public:
    enum class State { STARTED, STOPPED, ABANDONED };

    Waiter(async_dispatcher_t* dispatcher, zx::event event, zx_status_t trigger,
           Callback callback);
    ~Waiter();

    void set_state(State state) { state_ = state; }
    State state() const { return state_; }

    async::WaitBase& wait() { return wait_; }
    const zx::event& event() const { return event_; }

   private:
    void Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                const zx_packet_signal_t* signal);

    async_dispatcher_t* const dispatcher_;
    zx::event event_;
    Callback callback_;
    State state_ = State::STOPPED;
    async::WaitMethod<Waiter, &Waiter::Handle> wait_{this};
  };

  // Posts this EventTimestamper as a task on the background thread; when the
  // task is run it will bump the thread priority.
  // TODO(MZ-257): Avoid using a high-priority thread.  This would probably
  // entail not using a background thread at all, but instead relying on new
  // kernel functionality to add a timestamp to a port message and/or a signaled
  // event.  When addressing this, be sure to stop inheriting from async::Task.
  // Also see MG-940 and MG-1032.
  void IncreaseBackgroundThreadPriority();

  async_dispatcher_t* const main_dispatcher_;
  async::Loop background_loop_;
  async::TaskClosure task_;
#ifndef NDEBUG
  size_t watch_count_ = 0;
#endif

  FXL_DISALLOW_COPY_AND_ASSIGN(EventTimestamper);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_UTIL_EVENT_TIMESTAMPER_H_

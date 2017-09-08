// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include <async/loop.h>
#include <async/task.h>
#include <async/wait.h>
#include <mx/event.h>

#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace scene_manager {

// EventTimestamper uses a background thread to watch for signals specified
// by EventTimestamper::Watch objects.  When a signal is observed, a task is
// posted on the main loop to invoke a callback that is provided by the client.
//
// A program typically needs/wants a single EventTimestamper, which is shared
// by everyone who needs event-timestamps.
class EventTimestamper {
 private:
  class Wait;

 public:
  using Callback = std::function<void(mx_time_t timestamp)>;

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
    Watch(EventTimestamper* ts,
          mx::event event,
          mx_status_t trigger,
          Callback callback);
    Watch(Watch&&);
    ~Watch();

    // Start watching for the event to be signaled.  It is illegal to call
    // Start() again before the callback has been invoked (it is safe to invoke
    // Start() again from within the callback).
    void Start();

   private:
    Wait* wait_;
    EventTimestamper* timestamper_;

    FTL_DISALLOW_COPY_AND_ASSIGN(Watch);
  };

 private:
  // Helper object that stores state corresponding to a single Watch object.
  // Invariants:
  // - |state_| only changes on the main thread.
  // - instances of Wait are only destroyed on the main thread.
  class Wait {
   public:
    enum class State { STARTED, STOPPED, ABANDONED };

    Wait(const ftl::RefPtr<ftl::TaskRunner>& task_runner,
         mx::event event,
         mx_status_t trigger,
         Callback callback);
    ~Wait();

    void set_state(State state) { state_ = state; }
    State state() const { return state_; }

    async::Wait& wait() { return wait_; }

   private:
    async_wait_result_t Handle(async_t* async,
                               mx_status_t status,
                               const mx_packet_signal_t* signal);

    ftl::RefPtr<ftl::TaskRunner> task_runner_;
    mx::event event_;
    Callback callback_;
    State state_ = State::STOPPED;
    async::Wait wait_;
  };

  // Posts this EventTimestamper as a task on the background thread; when the
  // task is run it will bump the thread priority.
  // TODO(MZ-257): Avoid using a high-priority thread.  This would probably
  // entail not using a background thread at all, but instead relying on new
  // kernel functionality to add a timestamp to a port message and/or a signaled
  // event.  When addressing this, be sure to stop inheriting from async::Task.
  // Also see MG-940 and MG-1032.
  void IncreaseBackgroundThreadPriority();

  mtl::MessageLoop* const main_loop_;
  async::Loop background_loop_;
  async::Task task_;
#ifndef NDEBUG
  size_t watch_count_ = 0;
#endif

  FTL_DISALLOW_COPY_AND_ASSIGN(EventTimestamper);
};

}  // namespace scene_manager

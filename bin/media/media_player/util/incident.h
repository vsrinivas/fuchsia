// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_UTIL_INCIDENT_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_UTIL_INCIDENT_H_

#include <mutex>
#include <vector>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "lib/fxl/synchronization/thread_annotations.h"

// The Incident class provides a facility for executing code as the consequence
// of some occurrence. This can be useful for building state machines and
// otherwise dealing with asynchronous operations.
//
// Incident is not a thread-safe class and has no ability to make a thread wait
// or to execute code on a particular thread.
//
// Incidents rely heavily on fit::function, so they shouldn't be used in
// enormous numbers.
//
// An Incident can be in one of two states: initial state or occurred state.
//
// Code can be executed when an incident occurs:
//
//     incident.When([]() {
//       // Do something...
//     });
//
// The behavior of the When method depends on the incident's state. In initial
// state, the consequence is added to a list to be executed when the incident
// occurs. In occurred state, When executes the consequence.
//
// If a dispatcher is provided in the constructor, all consequences are posted to
// that dispatcher. If no dispatcher is provided, consequences queued prior to the Occur
// call are called synchronously from the Occur call, and consequences for
// When calls in the occurred state are called synchronously from the When
// calls.
//
// An Incident occurs when its Occur (or Run) method is invoked and the Incident
// is in the initial state. All registered consequences of the Incident are
// executed during the call to Occur in the order they were added. Subsequent
// calls to Occur are ignored until the Incident is reset.
//
// The Reset method ensures that the Incident is in its initial state and that
// the list of consequences is cleared (without running the consequences).
class Incident {
 public:
  Incident(async_dispatcher_t* dispatcher = nullptr);

  ~Incident();

  // Determines if this Incident has occurred due to a past call to Occur.
  bool occurred() { return occurred_; }

  // Executes the consequence when this Incident occurs. If this Incident hasn't
  // occurred when this method is called, a copy of the consequence is held
  // until this Incident occurs or is reset. If this Incident has occurred when
  // this method is called, the consequence is executed immediately and no copy
  // of the consequence is held.
  void When(fit::closure consequence) {
    if (occurred_) {
      InvokeConsequence(std::move(consequence));
    } else {
      consequences_.push_back(std::move(consequence));
    }
  }

  // If this Incident is in inital state (!occurred()), this method makes this
  // Incident occur, executing and deleting all its consequences. Otherwise,
  // does nothing.
  void Occur();

  // Resets this Incident to initial state and clears the list of consequences.
  void Reset() {
    occurred_ = false;
    consequences_.clear();
  }

 private:
  void InvokeConsequence(fit::closure consequence);

  async_dispatcher_t* dispatcher_;
  bool occurred_ = false;
  std::vector<fit::closure> consequences_;
};

// Like Incident, but threadsafe.
class ThreadsafeIncident {
 public:
  ThreadsafeIncident();

  ~ThreadsafeIncident();

  // Determines if this ThreadsafeIncident has occurred due to a past call to
  // Occur. Note that the state of the this ThreadsafeIncident may change
  // immediately after this method returns, so there's no guarantee that the
  // result is still valid.
  bool occurred() {
    std::lock_guard<std::mutex> locker(mutex_);
    return occurred_;
  }

  // Executes the consequence when this ThreadsafeIncident occurs. If this
  // ThreadsafeIncident hasn't occurred when this method is called, a copy of
  // the consequence is held until this ThreadsafeIncident occurs or is reset.
  // If this ThreadsafeIncident has occurred when this method is called, the
  // consequence is executed immediately and no copy of the consequence is held.
  // Note that this ThreadsafeIncident's internal lock is not held when the
  // consequence is called. It's therefore possible for this ThreadsafeIncident
  // to be reset between the time the decision is made to run the consequence
  // and when the consequence is actually run.
  void When(fit::closure consequence) {
    {
      std::lock_guard<std::mutex> locker(mutex_);
      if (!occurred_) {
        consequences_.push_back(std::move(consequence));
        return;
      }
    }

    consequence();
  }

  // If this ThreadsafeIncident is in inital state (!occurred()), this method
  // makes this ThreadsafeIncident occur, executing and deleting all its
  // consequences. Otherwise, does nothing.
  void Occur();

  // Resets this ThreadsafeIncident to initial state and clears the list of
  // consequences.
  void Reset() {
    std::lock_guard<std::mutex> locker(mutex_);
    occurred_ = false;
    consequences_.clear();
  }

 private:
  mutable std::mutex mutex_;
  bool occurred_ FXL_GUARDED_BY(mutex_) = false;
  std::vector<fit::closure> consequences_ FXL_GUARDED_BY(mutex_);
};

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_UTIL_INCIDENT_H_

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"

// The Incident class provides a facility for executing code as the consequence
// of some occurrence. This can be useful for building state machines and
// otherwise dealing with asynchronous operations.
//
// Incident is not a thread-safe class and has no ability to make a thread wait
// or to execute code on a particular thread.
//
// Incidents rely heavily on std::function, so they shouldn't be used in
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
// occurs. In occurred state, When executes the consequence immediately (before
// When returns).
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
  Incident();

  ~Incident();

  // Determines if this Incident has occurred due to a past call to Occur.
  bool occurred() { return occurred_; }

  // Executes the consequence when this Incident occurs. If this Incident hasn't
  // occurred when this method is called, a copy of the consequence is held
  // until this Incident occurs or is reset. If this Incident has occurred when
  // this method is called, the consequence is executed immediately and no copy
  // of the consequence is held.
  void When(const std::function<void()>& consequence) {
    if (occurred_) {
      consequence();
    } else {
      consequences_.push_back(consequence);
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
  bool occurred_ = false;
  std::vector<std::function<void()>> consequences_;
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
    fxl::MutexLocker locker(&mutex_);
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
  void When(const std::function<void()>& consequence) {
    {
      fxl::MutexLocker locker(&mutex_);
      if (!occurred_) {
        consequences_.push_back(consequence);
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
    fxl::MutexLocker locker(&mutex_);
    occurred_ = false;
    consequences_.clear();
  }

 private:
  mutable fxl::Mutex mutex_;
  bool occurred_ FXL_GUARDED_BY(mutex_) = false;
  std::vector<std::function<void()>> consequences_ FXL_GUARDED_BY(mutex_);
};

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class Err;
class Target;
class Thread;

class Breakpoint : public ClientObject {
 public:
  // The scope is what this breakpoint applies to.
  enum class Scope {
    // For session scopes, all processes attempt to resolve this breakpoint if
    // a symbol matches. You can't have an address breakpoint applying to all
    // processes (since addresses typically won't match between processes).
    kSystem,
    kTarget,
    kThread
  };

  // What stops when this breakpoint is hit.
  enum class Stop {
    kAll,      // All threads of all processes stop.
    kProcess,  // All threads of the process that hit the breakpoint stop.
    kThread    // Just the thread that hits the breakpoint stops.
  };

  explicit Breakpoint(Session* session);
  ~Breakpoint() override;

  void AddObserver(BreakpointObserver* observer);
  void RemoveObserver(BreakpointObserver* observer);

  // Sets whether this breakpoint is enabled. Disabled breakpoints still exist
  // on the client but are removed from the code. SetEnabled may return an
  // error if the breakpoint isn't in a state where it can be enabled (like
  // there is no valid location set on it).
  virtual bool IsEnabled() const = 0;
  virtual Err SetEnabled(bool enabled) = 0;

  // Sets/gets the scoping for this breakpoint. For both setter and getter:
  // - KSession: Process and thread are null.
  // - kTarget: Target is the desired target/process, thread is null.
  // - kThread: Target and thread are set.
  virtual Err SetScope(Scope scope, Target* target, Thread* thread) = 0;
  virtual Scope GetScope(Target** target, Thread** thread) const = 0;

  // Sets gets the stop mode.
  virtual void SetStopMode(Stop stop) = 0;
  virtual Stop GetStopMode() const = 0;

  // Sets the breakpoint location to the given address. Address breakpoints
  // must not be of Session scope (set the scope first).
  virtual void SetAddressLocation(uint64_t address) = 0;
  virtual uint64_t GetAddressLocation() const = 0;

  // Returns the number of times this breakpoint has been hit.
  virtual int GetHitCount() const = 0;

 protected:
  fxl::ObserverList<BreakpointObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<BreakpointObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

}  // namespace zxdb

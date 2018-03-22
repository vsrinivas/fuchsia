// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/lib/debug_ipc/records.h"
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

  explicit Breakpoint(Session* session);
  ~Breakpoint() override;

  void AddObserver(BreakpointObserver* observer);
  void RemoveObserver(BreakpointObserver* observer);

  // Commits all current settings to the breakpoint. Without calling this,
  // no breakpoint settings are send to the agent.
  //
  // The callback will be issued once the changes have taken effect. Be aware
  // that the breakpoint object could have been deleted by the time the
  // callback is issued, so don't retain references to it.
  virtual void CommitChanges(std::function<void(const Err&)> callback) = 0;

  // Sets whether this breakpoint is enabled. Disabled breakpoints still exist
  // on the client but are removed from the code.
  //
  // You must call CommitChanges before changes will have an effect.
  virtual bool IsEnabled() const = 0;
  virtual void SetEnabled(bool enabled) = 0;

  // Sets/gets the scoping for this breakpoint. For both setter and getter:
  // - KSession: Process and thread are null.
  // - kTarget: Target is the desired target/process, thread is null.
  // - kThread: Target and thread are set.
  //
  // You must call CommitChanges before changes will have an effect.
  virtual Err SetScope(Scope scope, Target* target, Thread* thread) = 0;
  virtual Scope GetScope(Target** target, Thread** thread) const = 0;

  // Sets gets the stop mode.
  //
  // You must call CommitChanges before changes will have an effect.
  virtual void SetStopMode(debug_ipc::Stop stop) = 0;
  virtual debug_ipc::Stop GetStopMode() const = 0;

  // Sets the breakpoint location to the given address. Address breakpoints
  // must not be of Session scope (set the scope first).
  //
  // You must call CommitChanges before changes will have an effect.
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

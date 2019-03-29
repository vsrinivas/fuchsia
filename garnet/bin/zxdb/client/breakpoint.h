// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/client_object.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"
#include "src/developer/debug/ipc/records.h"

namespace zxdb {

class BreakpointLocation;
class Err;
class Target;
class Thread;

class Breakpoint : public ClientObject {
 public:
  explicit Breakpoint(Session* session);
  ~Breakpoint() override;

  void AddObserver(BreakpointObserver* observer);
  void RemoveObserver(BreakpointObserver* observer);

  fxl::WeakPtr<Breakpoint> GetWeakPtr();

  // All of the settings, including the location, are stored in the
  // BreakpointSettings object. This API is designed so all settings changes
  // happen atomically. SetSettings() will always issue the callback, even
  // if the breakpoint has been destroyed. If you need to reference the
  // breakpoint object in the callback, get a weak pointer.
  virtual BreakpointSettings GetSettings() const = 0;
  virtual void SetSettings(const BreakpointSettings& settings,
                           std::function<void(const Err&)> callback) = 0;

  // Returns true if this is an internal breakpoint. Internal breakpoints are
  // used to implement other operations and are never exposed to the user.
  virtual bool IsInternal() const = 0;

  // Returns the locations associated with this breakpoint. These are the
  // actual addresses set. The symbols of these may not match the one in the
  // settings (for example, the line number might be different due to
  // optimization for each location).
  //
  // The returned pointers are owned by the Breakpoint and will be changed
  // if the settings or any process or module changes take place. Don't cache.
  virtual std::vector<BreakpointLocation*> GetLocations() = 0;

 protected:
  fxl::ObserverList<BreakpointObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<BreakpointObserver> observers_;
  fxl::WeakPtrFactory<Breakpoint> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

}  // namespace zxdb

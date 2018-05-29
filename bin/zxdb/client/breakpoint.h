// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint_observer.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
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
  explicit Breakpoint(Session* session);
  ~Breakpoint() override;

  void AddObserver(BreakpointObserver* observer);
  void RemoveObserver(BreakpointObserver* observer);

  // All of the settings, including the location, are stored in the
  // BreakpointSettings object. This API is designed so all settings changes
  // happen atomically.
  virtual BreakpointSettings GetSettings() const = 0;
  virtual void SetSettings(const BreakpointSettings& settings,
                           std::function<void(const Err&)> callback) = 0;

  // Returns the number of times this breakpoint has been hit.
  virtual int GetHitCount() const = 0;

 protected:
  fxl::ObserverList<BreakpointObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<BreakpointObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

}  // namespace zxdb

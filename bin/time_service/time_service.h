// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/time_service/fidl/time_service.fidl.h"

namespace time_service {

// Implementation of the FIDL time service. Handles setting/getting the raw
// timezone offset in minutes, with support for client watchers.
class TimeServiceImpl : public TimeService {
 public:
  TimeServiceImpl();
  ~TimeServiceImpl();

  // |TimeService|:
  void GetTimezoneOffsetMinutes(
      const GetTimezoneOffsetMinutesCallback &callback) override;
  void SetTimezoneOffsetMinutes(
      int64_t offset,
      const SetTimezoneOffsetMinutesCallback &callback) override;
  void Watch(fidl::InterfaceHandle<TimeServiceWatcher> watcher) override;
  void AddBinding(fidl::InterfaceRequest<TimeService> request);

 private:
  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(TimeServiceWatcher *watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(int64_t offset_change);

  fidl::BindingSet<TimeService> bindings_;
  std::vector<TimeServiceWatcherPtr> watchers_;
};

}  // namespace time_service

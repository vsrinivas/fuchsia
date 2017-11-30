// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/time_service/fidl/time_service.fidl.h"
#include "third_party/icu/source/common/unicode/strenum.h"

namespace app {
class ApplicationContext;
}

namespace time_service {

// Implementation of the FIDL time service. Handles setting/getting the timezone
// offset by ICU timezone ID.  Also supports getting the raw UTC offset in
// minutes.
//
// For information on ICU ID's and timezone information see:
// http://userguide.icu-project.org/formatparse/datetime
class TimeServiceImpl : public TimeService {
 public:
  // Constructs the time service with a caller-owned application context.
  TimeServiceImpl();
  ~TimeServiceImpl();

  // |TimeService|:
  void GetTimezoneOffsetMinutes(
      int64_t milliseconds,
      const GetTimezoneOffsetMinutesCallback& callback) override;
  void SetTimezone(const fidl::String& timezone_id,
                   const SetTimezoneCallback& callback) override;
  void GetTimezoneId(const GetTimezoneIdCallback& callback) override;
  void Watch(fidl::InterfaceHandle<TimeServiceWatcher> watcher) override;

  void AddBinding(fidl::InterfaceRequest<TimeService> request);

 private:
  bool Init();
  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(TimeServiceWatcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const fidl::String& new_timezone_id);
  // Returns true if |timezone_id| is a valid timezone.
  bool IsValidTimezoneId(const fidl::String& timezone_id);
  // Private implementation of TimeServiceImpl::GetTimezoneId, for use in other
  // methods. Returns a guaranteed-valid timezone ID.
  fidl::String GetTimezoneIdImpl();

  // Set to true iff |icu_data_| has been mapped, and the data contained therein
  // is the correct format (when Init() is successful).
  bool valid_;
  fidl::BindingSet<TimeService> bindings_;
  std::vector<TimeServiceWatcherPtr> watchers_;
};

}  // namespace time_service

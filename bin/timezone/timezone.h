// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TIMEZONE_TIMEZONE_H_
#define GARNET_BIN_TIMEZONE_TIMEZONE_H_

#include <vector>

#include <fuchsia/timezone/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "third_party/icu/source/common/unicode/strenum.h"

namespace time_zone {

// Implementation of the FIDL time service. Handles setting/getting the
// timezone offset by ICU timezone ID.  Also supports getting the raw UTC
// offset in minutes.
//
// For information on ICU ID's and timezone information see:
// http://userguide.icu-project.org/formatparse/datetime
class TimezoneImpl : public fuchsia::timezone::Timezone {
 public:
  // Constructs the time service with a caller-owned application context.
  TimezoneImpl(std::unique_ptr<component::StartupContext> context,
               const char icu_data_path[], const char tz_id_path[]);
  ~TimezoneImpl();

  // |Timezone|:
  void GetTimezoneOffsetMinutes(
      int64_t milliseconds, GetTimezoneOffsetMinutesCallback callback) override;
  void SetTimezone(fidl::StringPtr timezone_id,
                   SetTimezoneCallback callback) override;
  void GetTimezoneId(GetTimezoneIdCallback callback) override;
  void Watch(fidl::InterfaceHandle<fuchsia::timezone::TimezoneWatcher> watcher)
      override;

 private:
  bool Init();
  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::timezone::TimezoneWatcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const fidl::StringPtr& new_timezone_id);
  // Returns true if |timezone_id| is a valid timezone.
  bool IsValidTimezoneId(const fidl::StringPtr& timezone_id);
  // Private implementation of TimezoneImpl::GetTimezoneId, for use in other
  // methods. Returns a guaranteed-valid timezone ID.
  fidl::StringPtr GetTimezoneIdImpl();

  std::unique_ptr<component::StartupContext> context_;
  const char* const icu_data_path_;
  const char* const tz_id_path_;

  // Set to true iff |icu_data_| has been mapped, and the data contained therein
  // is the correct format (when Init() is successful).
  bool valid_;
  fidl::BindingSet<fuchsia::timezone::Timezone> bindings_;
  std::vector<fuchsia::timezone::TimezoneWatcherPtr> watchers_;
};

}  // namespace time_zone

#endif  // GARNET_BIN_TIMEZONE_TIMEZONE_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TIMEZONE_TIMEZONE_H_
#define GARNET_BIN_TIMEZONE_TIMEZONE_H_

#include <fuchsia/deprecatedtimezone/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "third_party/icu/source/common/unicode/strenum.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace time_zone {

// Implementation of the FIDL time service. Handles setting/getting the
// timezone offset by ICU timezone ID.  Also supports getting the raw UTC
// offset in minutes.
//
// For information on ICU ID's and timezone information see:
// http://userguide.icu-project.org/formatparse/datetime
class TimezoneImpl : public fuchsia::deprecatedtimezone::Timezone {
  using fuchsia::deprecatedtimezone::Timezone::GetTimezoneIdCallback;
  using fuchsia::deprecatedtimezone::Timezone::GetTimezoneOffsetMinutesCallback;
  using fuchsia::deprecatedtimezone::Timezone::SetTimezoneCallback;

 public:
  // Constructs the time service with a caller-owned application context.
  TimezoneImpl(std::unique_ptr<sys::ComponentContext> context, const char icu_data_path[],
               const char tz_id_path[]);
  ~TimezoneImpl();

  // |fuchsia.deprecatedtimezone.Timezone|:
  void GetTimezoneOffsetMinutes(int64_t milliseconds,
                                GetTimezoneOffsetMinutesCallback callback) override;
  void SetTimezone(std::string timezone_id, SetTimezoneCallback callback) override;
  void GetTimezoneId(GetTimezoneIdCallback callback) override;
  void Watch(fidl::InterfaceHandle<fuchsia::deprecatedtimezone::TimezoneWatcher> watcher) override;

 private:
  bool Init();
  // Destroys a watcher proxy (called upon a connection error).
  void ReleaseWatcher(fuchsia::deprecatedtimezone::TimezoneWatcher* watcher);
  // Alerts all watchers when an update has occurred.
  void NotifyWatchers(const std::string& new_timezone_id);

  // Loads the stored timezone, if any, and sets up the cached state.
  void LoadTimezone();

  // Returns true and an allocated instance of icu::TimeZone if |timezone_id| is
  // a valid timezone.
  static std::pair<bool, std::unique_ptr<icu::TimeZone>> ValidateTimezoneId(
      const std::string& timezone_id);

  std::unique_ptr<sys::ComponentContext> context_;
  const char* const icu_data_path_;
  const char* const tz_id_path_;

  // Set to true iff |icu_data_| has been mapped, and the data contained therein
  // is the correct format (when Init() is successful).
  bool valid_;

  // Cached id and timezone object to avoid performing file IO and allocation on
  // every request.
  struct State {
    std::string timezone_id;
    std::unique_ptr<icu::TimeZone> timezone;
  };
  std::optional<State> cached_state_;

  // |fuchsia.deprecatedtimezone.Timezone|:
  fidl::BindingSet<fuchsia::deprecatedtimezone::Timezone> deprecated_bindings_;
  std::vector<fuchsia::deprecatedtimezone::TimezoneWatcherPtr> deprecated_watchers_;
};

}  // namespace time_zone

#endif  // GARNET_BIN_TIMEZONE_TIMEZONE_H_

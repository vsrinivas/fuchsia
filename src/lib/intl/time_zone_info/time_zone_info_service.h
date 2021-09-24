// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INTL_TIME_ZONE_INFO_TIME_ZONE_INFO_SERVICE_H_
#define SRC_LIB_INTL_TIME_ZONE_INFO_TIME_ZONE_INFO_SERVICE_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/zx/time.h>

#include <variant>

#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"

namespace intl {

// Implementation of `fuchsia.intl.TimeZones`.
//
// Provides information about time zones.
//
// Usage example:
//
// ```
// #include "src/lib/intl/time_zone_info/time_zone_info_service.h"
//
// #include <lib/async-loop/cpp/loop.h>
// #include <lib/async-loop/default.h>
// #include <lib/sys/cpp/component_context.h>
//
// async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
// auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
// auto tz_svc = TimeZoneInfoService::Create();
// // Starts serving `fuchsia.intl.TimeZones`
// context->outgoing()->AddPublicService(tz_svc->GetHandler());
// tz_svc->Start();
// loop.Run();
// ```
//
class TimeZoneInfoService final : fuchsia::intl::TimeZones {
 public:
  // Creates an instance of `TimeZoneInfoService`. The returned service instance is not ready to
  // respond to incoming requests until `Start()` is called.
  static std::unique_ptr<TimeZoneInfoService> Create();

  // Returns the client-side handler for `fuchsia.intl.TimeZones`, based on either the
  // `dispatcher` that is passed in (e.g. for testing), or the default thread-local dispatcher if
  // `dispatcher` is omitted or null.
  //
  // Ownership of `dispatcher` is retained by the caller.
  fidl::InterfaceRequestHandler<fuchsia::intl::TimeZones> GetHandler(
      async_dispatcher_t* dispatcher = nullptr);

  // Performs required initialization of the service. This method *must* be called before the
  // service is added to the component's outgoing directory.
  void Start();

  // `fuchsia.intl.TimeZones`
  void AbsoluteToCivilTime(fuchsia::intl::TimeZoneId time_zone, zx_time_t absolute_time,
                           AbsoluteToCivilTimeCallback callback) override;

  // `fuchsia.intl.TimeZones`
  void CivilToAbsoluteTime(fuchsia::intl::CivilTime civil_time,
                           fuchsia::intl::CivilToAbsoluteTimeOptions options,
                           CivilToAbsoluteTimeCallback callback) override;

  // `fuchsia.intl.TimeZones`
  void GetTimeZoneInfo(fuchsia::intl::TimeZoneId time_zone_id, zx_time_t at_time,
                       GetTimeZoneInfoCallback callback) override;

 private:
  // Logs the given ICU error at the appropriate severity level, and returns a corresponding
  // `TimeZonesError` enum value.
  //
  // Note that `civil_time`'s ownership is retained by the caller.
  std::optional<fuchsia::intl::TimeZonesError> ConvertAndLogICUError(
      UErrorCode icu_status, const fuchsia::intl::CivilTime* civil_time = nullptr,
      std::optional<zx_time_t> absolute_time = std::nullopt);

  // Attempts to load a calendar for the given time zone. If the loading fails, returns a
  // `fuchsia::intl::TimeZonesError`.
  std::variant<std::unique_ptr<icu::Calendar>, fuchsia::intl::TimeZonesError> LoadCalendar(
      const fuchsia::intl::TimeZoneId& time_zone_id);

  fidl::BindingSet<fuchsia::intl::TimeZones> bindings_;
};

}  // namespace intl

#endif  // SRC_LIB_INTL_TIME_ZONE_INFO_TIME_ZONE_INFO_SERVICE_H_

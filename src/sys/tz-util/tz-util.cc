// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls.h>

#include <iostream>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

static constexpr char kGetOffsetCmd[] = "get_offset_minutes";
static constexpr char kSetTimezoneIdCmd[] = "set_timezone_id";
static constexpr char kGetTimezoneIdCmd[] = "get_timezone_id";

class TzUtil {
 public:
  TzUtil(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->svc()->Connect(intl_settings_.NewRequest());
  }

  void Run(fxl::CommandLine command_line) {
    if (command_line.HasOption("help")) {
      Usage();
      return;
    }
    const zx_status_t initial_values_status = intl_settings_->Watch(&initial_values_);
    if (initial_values_status != ZX_OK || initial_values_.is_err()) {
      std::cerr << "ERROR: could not request initial settings: " << initial_values_status
                << std::endl;
      return;
    }
    if (command_line.HasOption(kSetTimezoneIdCmd)) {
      std::string timezone_id;
      command_line.GetOptionValue(kSetTimezoneIdCmd, &timezone_id);
      if (!timezone_id.empty()) {
        fuchsia::settings::Intl_Set_Result result;
        const fuchsia::intl::TimeZoneId new_tzid{
            .id = timezone_id,
        };
        fuchsia::settings::IntlSettings new_setting;
        new_setting.set_time_zone_id(new_tzid);
        if (intl_settings_->Set(std::move(new_setting), &result) != ZX_OK || result.is_err()) {
          std::cerr << "ERROR: Unable to set ID: " << static_cast<uint32_t>(result.err())
                    << std::endl;
          exit(1);
        }
        return;
      } else {
        Usage();
      }
      return;
    }
    if (command_line.HasOption(kGetTimezoneIdCmd)) {
      std::cerr << GetTimezoneName() << std::endl;
      return;
    }
    if (command_line.HasOption(kGetOffsetCmd)) {
      zx_time_t milliseconds_since_epoch = 0;
      zx_clock_get(ZX_CLOCK_UTC, &milliseconds_since_epoch);
      milliseconds_since_epoch /= ZX_MSEC(1);
      const auto result = GetTimezoneOffsetMinutes(milliseconds_since_epoch);
      if (result.is_error()) {
        std::cerr << "ERROR: Unable to get offset." << std::endl;
      } else {
        std::cerr << result.value() << std::endl;
      }
      return;
    }

    // Default: no args.
    Usage();
  }

 private:
  static void Usage() {
    std::cout << "Usage: tz-util [--help|"
              << "--" << kSetTimezoneIdCmd << "=ID|"
              << "--" << kGetTimezoneIdCmd << "|"
              << "--" << kGetOffsetCmd << "]" << std::endl;
    std::cout << std::endl;
  }

  // Returns the timezone offset, expressed in minutes.
  fit::result<int32_t, std::string> GetTimezoneOffsetMinutes(int32_t ms_since_epoch) {
    const std::string timezone_id = GetTimezoneName();
    std::unique_ptr<icu::TimeZone> timezone(icu::TimeZone::createTimeZone(timezone_id.c_str()));
    UErrorCode error = U_ZERO_ERROR;
    const auto udate_since_epoch = static_cast<UDate>(ms_since_epoch);
    int32_t local_offset_ms = 0;
    int32_t dst_offset_ms = 0;
    timezone->getOffset(udate_since_epoch, false, local_offset_ms, dst_offset_ms, error);
    if (error != U_ZERO_ERROR) {
      icu::ErrorCode icu_error;
      icu_error.set(error);
      return fit::error(icu_error.errorName());
    }
    static constexpr int32_t kMSPerMin = 60 * 1000;
    return fit::ok((local_offset_ms + dst_offset_ms) / kMSPerMin);
  }

  // Returns the timezone name; falls back to UTC if unset.
  std::string GetTimezoneName() {
    const auto& settings = initial_values_.response().settings;
    if (!settings.has_time_zone_id()) {
      return "UTC";
    }
    const std::string& id = settings.time_zone_id().id;
    if (id.empty()) {
      return "UTC";
    }
    return settings.time_zone_id().id;
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::settings::Intl_Watch_Result initial_values_;
  fuchsia::settings::IntlSyncPtr intl_settings_;
};

int main(int argc, char** argv) {
  const zx_status_t init_status = icu_data::Initialize();
  ZX_ASSERT_MSG(init_status == ZX_OK, "Status was: %d", init_status);
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }
  // loop is needed by StartupContext.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  TzUtil app(sys::ComponentContext::Create());
  app.Run(command_line);
  return 0;
}

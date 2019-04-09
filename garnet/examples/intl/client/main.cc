// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/component_context.h>

#include <iostream>

#include "fuchsia/examples/intl/wisdom/cpp/fidl.h"
#include "intl_wisdom_client.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async/default.h"
#include "lib/zx/process.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/icu_data/cpp/icu_data.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/gregocal.h"
#include "third_party/icu/source/i18n/unicode/smpdtfmt.h"
#include "zircon/processargs.h"

using icu::GregorianCalendar;
using icu::ParsePosition;
using icu::SimpleDateFormat;
using icu::TimeZone;
using icu::UnicodeString;

// The default is an arbitrary afternoon in October.
constexpr char kDefaultTimeString[] = "2018-10-30T15:30:00-07:00";
constexpr char kDefaultTimeZoneString[] = "Etc/Unknown";

std::unique_ptr<TimeZone> ParseOrGetDefaultTimeZone(
    const std::string& time_zone_id) {
  auto time_zone = std::unique_ptr<TimeZone>(
      TimeZone::createTimeZone(UnicodeString::fromUTF8(time_zone_id)));
  if (TimeZone::getUnknown() == *time_zone) {
    time_zone = std::unique_ptr<TimeZone>(TimeZone::detectHostTimeZone());
  }
  return time_zone;
}

zx::time ParseTimestamp(const std::string& time_string) {
  UnicodeString time_string_unic = UnicodeString::fromUTF8(time_string);

  UErrorCode error_code = U_ZERO_ERROR;
  SimpleDateFormat time_parser(UnicodeString("yyyy-MM-dd'T'HH:mm:ssXX"),
                               error_code);
  ZX_ASSERT(U_SUCCESS(error_code));

  // Uses the system time zone
  GregorianCalendar calendar(error_code);
  ZX_ASSERT(U_SUCCESS(error_code));

  ParsePosition parse_position;
  time_parser.parse(time_string_unic, calendar, parse_position);
  UDate parsed_time = calendar.getTime(error_code);

  if (U_FAILURE(error_code) || parse_position.getErrorIndex() != -1) {
    ZX_PANIC("Failed to parse '%s'", time_string.c_str());
  }

  zx::time timestamp(static_cast<zx_time_t>(parsed_time));
  return timestamp;
}

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const std::string server_url = command_line.GetOptionValueWithDefault(
      "server",
      "fuchsia-pkg://fuchsia.com/intl_wisdom#meta/intl_wisdom_server.cmx");
  const std::string time_string =
      command_line.GetOptionValueWithDefault("timestamp", kDefaultTimeString);
  const std::string time_zone_id = command_line.GetOptionValueWithDefault(
      "timezone", kDefaultTimeZoneString);

  // We need to initialize ICU data in order to be able to parse |time_string|.
  ZX_ASSERT(icu_data::Initialize());

  zx::time timestamp = ParseTimestamp(time_string);
  auto time_zone = ParseOrGetDefaultTimeZone(time_zone_id);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  intl_wisdom::IntlWisdomClient client(sys::ComponentContext::Create());
  client.Start(server_url);
  client.SendRequest(timestamp, *time_zone, [&loop](fidl::StringPtr response) {
    printf("Response:\n%s\n", response->data());
    loop.Quit();
  });

  loop.Run();
  return 0;
}

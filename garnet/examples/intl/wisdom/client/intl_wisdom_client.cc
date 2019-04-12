// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#include "intl_wisdom_client.h"

#include <lib/sys/cpp/component_context.h>

#include "fuchsia/examples/intl/wisdom/cpp/fidl.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/calendar.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "third_party/icu/source/i18n/unicode/tzfmt.h"

namespace intl_wisdom {

using fuchsia::examples::intl::wisdom::IntlWisdomServer;
using fuchsia::intl::CalendarId;
using fuchsia::intl::LocaleId;
using fuchsia::intl::Profile;
using fuchsia::intl::ProfilePtr;
using fuchsia::intl::TimeZoneId;
using icu::Calendar;
using icu::Locale;
using icu::TimeZone;
using icu::TimeZoneFormat;
using icu::UnicodeString;

namespace {
// Gets a five-character time zone key (e.g. "usnyc"), used in Unicode BCP 47
// Locale IDs, for the given time zone.
std::string GetShortTimeZoneKey(const TimeZone& time_zone) {
  UErrorCode error_code = U_ZERO_ERROR;
  auto format = std::unique_ptr<TimeZoneFormat>(
      TimeZoneFormat::createInstance(Locale::getUS(), error_code));
  UnicodeString output;
  format->format(UTimeZoneFormatStyle::UTZFMT_STYLE_ZONE_ID_SHORT, time_zone,
                 Calendar::getNow(), output);
  std::string output_utf8;
  output.toUTF8String(output_utf8);
  return output_utf8;
}
}  // namespace

IntlWisdomClient::IntlWisdomClient(
    std::unique_ptr<sys::ComponentContext> startup_context)
    : startup_context_(std::move(startup_context)) {}

void IntlWisdomClient::Start(std::string server_url) {
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = server_url;
  launch_info.directory_request = directory.NewRequest().TakeChannel();
  fuchsia::sys::LauncherPtr launcher;
  startup_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  sys::ServiceDirectory services(std::move(directory));
  services.Connect(server_.NewRequest());
}

ProfilePtr MakeIntlProfile(const TimeZone& time_zone) {
  auto intl_profile = Profile::New();

  const std::string time_zone_key = GetShortTimeZoneKey(time_zone);

  fidl::VectorPtr<LocaleId> locales;
  {
    LocaleId locale_id;
    locale_id.id =
        "fr-FR-u-ca-hebrew-fw-tuesday-nu-traditio-tz-" + time_zone_key;
    locales.push_back(locale_id);
  }
  {
    LocaleId locale_id;
    locale_id.id =
        "es-MX-u-ca-hebrew-fw-tuesday-nu-traditio-tz-" + time_zone_key;
    locales.push_back(locale_id);
  }
  {
    LocaleId locale_id;
    locale_id.id =
        "ru-PT-u-ca-hebrew-fw-tuesday-nu-traditio-tz-" + time_zone_key;
    locales.push_back(locale_id);
  }
  {
    LocaleId locale_id;
    locale_id.id =
        "ar-AU-u-ca-hebrew-fw-tuesday-nu-traditio-tz-" + time_zone_key;
    locales.push_back(locale_id);
  }
  intl_profile->set_locales(std::move(locales));

  fidl::VectorPtr<CalendarId> calendars;
  {
    CalendarId calendar_id;
    calendar_id.id = "und-u-ca-hebrew";
    calendars.push_back(calendar_id);
  }
  {
    CalendarId calendar_id;
    calendar_id.id = "und-u-ca-gregorian";
    calendars.push_back(calendar_id);
  }
  {
    CalendarId calendar_id;
    calendar_id.id = "und-u-ca-islamic";
    calendars.push_back(calendar_id);
  }
  intl_profile->set_calendars(std::move(calendars));

  fidl::VectorPtr<TimeZoneId> time_zones;
  {
    UnicodeString id_unic;
    // This is the IANA Time Zone ID, e.g. "America/New_York".
    time_zone.getID(id_unic);
    std::string id_utf8;
    id_unic.toUTF8String(id_utf8);

    TimeZoneId time_zone_id;
    time_zone_id.id = id_utf8;
    time_zones.push_back(time_zone_id);
  }
  intl_profile->set_time_zones(std::move(time_zones));

  return intl_profile;
}

void IntlWisdomClient::SendRequest(
    zx::time timestamp, const TimeZone& time_zone,
    IntlWisdomServer::AskForWisdomCallback callback) const {
  ProfilePtr intl_profile = MakeIntlProfile(time_zone);
  printf("Asking for wisdom...\n");
  server()->AskForWisdom(std::move(*intl_profile), timestamp.get(),
                         std::move(callback));
}

}  // namespace intl_wisdom
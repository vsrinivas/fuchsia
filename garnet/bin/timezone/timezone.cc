// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/timezone/timezone.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <fstream>
#include <optional>
#include <utility>

#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/common/unicode/udata.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

constexpr char kDefaultTimezone[] = "UTC";
constexpr int32_t kMillisecondsInMinute = 60000;

namespace time_zone {

TimezoneImpl::TimezoneImpl(std::unique_ptr<sys::ComponentContext> context,
                           const char icu_data_path[], const char tz_id_path[])
    : context_(std::move(context)),
      icu_data_path_(icu_data_path),
      tz_id_path_(tz_id_path),
      valid_(Init()),
      inspector_(context_.get()),
      timezone_property_(inspector_.root().CreateString("timezone", "")) {
  if (valid_) {
    inspector_.Health().Ok();
    LoadTimezone();
  }
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));
}

TimezoneImpl::~TimezoneImpl() = default;

bool TimezoneImpl::Init() {
  fsl::SizedVmo icu_data;
  if (!fsl::VmoFromFilename(icu_data_path_, &icu_data)) {
    FX_LOGS(ERROR) << "Unable to load ICU data. Timezone data unavailable.";
    inspector_.Health().Unhealthy("Unable to load ICU data.");
    return false;
  }

  // Maps the ICU VMO into this process.
  uintptr_t icu_data_ptr = 0;
  if (zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, icu_data.vmo().get(), 0, icu_data.size(),
                  &icu_data_ptr) != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to map ICU data into process.";
    inspector_.Health().Unhealthy("Unable to map ICU data into process.");
    return false;
  }

  // ICU-related initialization.
  UErrorCode icu_set_data_status = U_ZERO_ERROR;
  udata_setCommonData(reinterpret_cast<void*>(icu_data_ptr), &icu_set_data_status);
  if (icu_set_data_status != U_ZERO_ERROR) {
    FX_LOGS(ERROR) << "Unable to set common ICU data. "
                   << "Timezone data unavailable.";
    inspector_.Health().Unhealthy("Timezone data unavailable");
    return false;
  }

  return true;
}

void TimezoneImpl::GetTimezoneOffsetMinutes(int64_t milliseconds_since_epoch,
                                            GetTimezoneOffsetMinutesCallback callback) {
  if (!valid_) {
    callback(0, 0);
    return;
  }

  // If valid_ is true, then cached_state_ must be valid.
  FX_CHECK(cached_state_.has_value());
  auto& timezone = cached_state_->timezone;

  int32_t local_offset = 0, dst_offset = 0;
  UErrorCode error = U_ZERO_ERROR;
  // Local time is set to false, and local_offset/dst_offset/error are mutated
  // via non-const references.
  timezone->getOffset(static_cast<UDate>(milliseconds_since_epoch), false, local_offset, dst_offset,
                      error);
  if (error != U_ZERO_ERROR) {
    icu::ErrorCode icuError;
    icuError.set(error);
    FX_LOGS(ERROR) << "Unable to get correct offset: error code " << error << " "
                   << icuError.errorName();
    callback(0, 0);
    return;
  }
  local_offset /= kMillisecondsInMinute;
  dst_offset /= kMillisecondsInMinute;
  callback(local_offset, dst_offset);
}

std::pair<bool, std::unique_ptr<icu::TimeZone>> TimezoneImpl::ValidateTimezoneId(
    const std::string& timezone_id) {
  std::unique_ptr<icu::TimeZone> timezone(icu::TimeZone::createTimeZone(timezone_id.c_str()));
  if ((*timezone) == icu::TimeZone::getUnknown()) {
    return {false, nullptr};
  }
  return {true, std::move(timezone)};
}

void TimezoneImpl::SetTimezone(std::string timezone_id, SetTimezoneCallback callback) {
  if (!valid_) {
    FX_LOGS(ERROR) << "Time service is not valid.";
    callback(false);
    return;
  }

  auto [is_valid, timezone] = ValidateTimezoneId(timezone_id);
  if (!is_valid) {
    FX_LOGS(ERROR) << "Timezone '" << timezone_id << "' is not valid.";
    callback(false);
    return;
  }

  timezone_property_.Set(timezone_id);
  cached_state_ = {timezone_id, std::move(timezone)};

  std::ofstream out_fstream(tz_id_path_, std::ofstream::trunc);
  if (!out_fstream.is_open()) {
    FX_LOGS(ERROR) << "Unable to open file for write '" << tz_id_path_ << "'";
    callback(false);
    return;
  }
  out_fstream << timezone_id;
  out_fstream.close();
  callback(true);
}

void TimezoneImpl::GetTimezoneId(GetTimezoneIdCallback callback) {
  callback(cached_state_ ? cached_state_->timezone_id : kDefaultTimezone);
}

void TimezoneImpl::LoadTimezone() {
  std::string timezone_id;

  std::ifstream in_fstream(tz_id_path_);
  if (!in_fstream.is_open()) {
    timezone_id = kDefaultTimezone;
  } else {
    in_fstream >> timezone_id;
    in_fstream.close();
  }

  if (timezone_id.empty()) {
    FX_LOGS(ERROR) << "TZ file empty at '" << tz_id_path_ << "'";
    inspector_.Health().Unhealthy("TZ file is empty");
    timezone_id = kDefaultTimezone;
  }

  auto [is_valid, timezone] = ValidateTimezoneId(timezone_id);
  if (!is_valid) {
    FX_LOGS(ERROR) << "Saved TZ ID invalid: '" << timezone_id << "'";
    inspector_.Health().Unhealthy("Saved TZ id is invalid");
    timezone_id = kDefaultTimezone;
    timezone = ValidateTimezoneId(kDefaultTimezone).second;
  }

  timezone_property_.Set(timezone_id);
  cached_state_ = {timezone_id, std::move(timezone)};
}

}  // namespace time_zone

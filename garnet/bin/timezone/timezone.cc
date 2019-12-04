// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/timezone/timezone.h"

#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls.h>

#include <fstream>

#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/logging.h"
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
      valid_(Init()) {
  context_->outgoing()->AddPublicService(deprecated_bindings_.GetHandler(this));
}

TimezoneImpl::~TimezoneImpl() = default;

bool TimezoneImpl::Init() {
  fsl::SizedVmo icu_data;
  if (!fsl::VmoFromFilename(icu_data_path_, &icu_data)) {
    FXL_LOG(ERROR) << "Unable to load ICU data. Timezone data unavailable.";
    return false;
  }

  // Maps the ICU VMO into this process.
  uintptr_t icu_data_ptr = 0;
  if (zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, icu_data.vmo().get(), 0, icu_data.size(),
                  &icu_data_ptr) != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map ICU data into process.";
    return false;
  }

  // ICU-related initialization.
  UErrorCode icu_set_data_status = U_ZERO_ERROR;
  udata_setCommonData(reinterpret_cast<void*>(icu_data_ptr), &icu_set_data_status);
  if (icu_set_data_status != U_ZERO_ERROR) {
    FXL_LOG(ERROR) << "Unable to set common ICU data. "
                   << "Timezone data unavailable.";
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
  std::string timezone_id = GetTimezoneIdImpl();
  std::unique_ptr<icu::TimeZone> timezone(icu::TimeZone::createTimeZone(timezone_id.c_str()));
  int32_t local_offset = 0, dst_offset = 0;
  UErrorCode error = U_ZERO_ERROR;
  // Local time is set to false, and local_offset/dst_offset/error are mutated
  // via non-const references.
  timezone->getOffset(static_cast<UDate>(milliseconds_since_epoch), false, local_offset, dst_offset,
                      error);
  if (error != U_ZERO_ERROR) {
    icu::ErrorCode icuError;
    icuError.set(error);
    FXL_LOG(ERROR) << "Unable to get correct offset: error code " << error << " "
                   << icuError.errorName();
    callback(0, 0);
    return;
  }
  local_offset /= kMillisecondsInMinute;
  dst_offset /= kMillisecondsInMinute;
  callback(local_offset, dst_offset);
}

void TimezoneImpl::NotifyWatchers(const std::string& new_timezone_id) {
  for (auto& watcher : deprecated_watchers_) {
    watcher->OnTimezoneOffsetChange(new_timezone_id);
  }
}

bool TimezoneImpl::IsValidTimezoneId(const std::string& timezone_id) {
  std::unique_ptr<icu::TimeZone> timezone(icu::TimeZone::createTimeZone(timezone_id.c_str()));
  if ((*timezone) == icu::TimeZone::getUnknown()) {
    return false;
  }
  return true;
}

void TimezoneImpl::SetTimezone(std::string timezone_id, SetTimezoneCallback callback) {
  if (!valid_) {
    FXL_LOG(ERROR) << "Time service is not valid.";
    callback(false);
    return;
  }
  if (!IsValidTimezoneId(timezone_id)) {
    FXL_LOG(ERROR) << "Timezone '" << timezone_id << "' is not valid.";
    callback(false);
    return;
  }
  std::ofstream out_fstream(tz_id_path_, std::ofstream::trunc);
  if (!out_fstream.is_open()) {
    FXL_LOG(ERROR) << "Unable to open file for write '" << tz_id_path_ << "'";
    callback(false);
    return;
  }
  out_fstream << timezone_id;
  out_fstream.close();
  NotifyWatchers(timezone_id);
  callback(true);
}

void TimezoneImpl::GetTimezoneId(GetTimezoneIdCallback callback) { callback(GetTimezoneIdImpl()); }

std::string TimezoneImpl::GetTimezoneIdImpl() {
  if (!valid_) {
    return kDefaultTimezone;
  }
  std::ifstream in_fstream(tz_id_path_);
  if (!in_fstream.is_open()) {
    return kDefaultTimezone;
  }
  std::string id_str;
  in_fstream >> id_str;
  in_fstream.close();

  if (id_str.empty()) {
    FXL_LOG(ERROR) << "TZ file empty at '" << tz_id_path_ << "'";
    return kDefaultTimezone;
  }
  if (!IsValidTimezoneId(id_str)) {
    FXL_LOG(ERROR) << "Saved TZ ID invalid: '" << id_str << "'";
    return kDefaultTimezone;
  }
  return id_str;
}

void TimezoneImpl::ReleaseWatcher(fuchsia::deprecatedtimezone::TimezoneWatcher* watcher) {
  auto predicate = [watcher](const auto& target) { return target.get() == watcher; };
  deprecated_watchers_.erase(
      std::remove_if(deprecated_watchers_.begin(), deprecated_watchers_.end(), predicate));
}

void TimezoneImpl::Watch(
    fidl::InterfaceHandle<fuchsia::deprecatedtimezone::TimezoneWatcher> watcher) {
  fuchsia::deprecatedtimezone::TimezoneWatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::deprecatedtimezone::TimezoneWatcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });
  deprecated_watchers_.push_back(std::move(watcher_proxy));
}

}  // namespace time_zone

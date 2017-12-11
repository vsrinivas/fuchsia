// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/time_service/time_service.h"

#include <zircon/syscalls.h>
#include <algorithm>
#include <fstream>
#include <memory>

#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/icu_data/cpp/constants.h"
#include "lib/icu_data/fidl/icu_data.fidl.h"
#include "third_party/icu/source/common/unicode/udata.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

static constexpr char kTzIdFile[] = "/data/tz_id";
static constexpr char kDefaultTimezone[] = "UTC";
static constexpr int32_t kMillisecondsInMinute = 60000;

namespace time_service {

TimeServiceImpl::TimeServiceImpl() : valid_(Init()) {}

TimeServiceImpl::~TimeServiceImpl() = default;

bool TimeServiceImpl::Init() {
  icu_data::ICUDataProviderSyncPtr icu_provider;
  icu_data::ICUDataPtr icu_data_out;
  fsl::SizedVmo icu_vmo;
  app::ConnectToEnvironmentService(GetSynchronousProxy(&icu_provider));
  icu_provider->ICUDataWithSha1(icu_data::kDataHash, &icu_data_out);
  if (!icu_data_out) {
    FXL_LOG(ERROR) << "Unable to load ICU data. Timezone data unavailable.";
    return false;
  }

  // Maps the ICU VMO into this process.
  fsl::SizedVmo::FromTransport(std::move(icu_data_out->vmo), &icu_vmo);
  uintptr_t icu_data_ptr = 0;
  if (zx_vmar_map(zx_vmar_root_self(), 0, icu_vmo.vmo().get(), 0,
                  icu_vmo.size(), ZX_VM_FLAG_PERM_READ,
                  &icu_data_ptr) != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map ICU data into process.";
    return false;
  }

  // ICU-related initialization.
  UErrorCode icu_set_data_status;
  udata_setCommonData(reinterpret_cast<void*>(icu_data_ptr),
                      &icu_set_data_status);
  if (icu_set_data_status != U_ZERO_ERROR) {
    FXL_LOG(ERROR) << "Unable to set common ICU data. "
                   << "Timezone data unavailable.";
    return false;
  }

  FXL_LOG(INFO) << "Time zone data initialized successfully.";
  return true;
}

void TimeServiceImpl::GetTimezoneOffsetMinutes(
    int64_t milliseconds_since_epoch,
    const GetTimezoneOffsetMinutesCallback& callback) {
  if (!valid_) {
    callback(0, 0);
    return;
  }
  fidl::String timezone_id = GetTimezoneIdImpl();
  std::unique_ptr<icu::TimeZone> timezone(
      icu::TimeZone::createTimeZone(timezone_id.get().c_str()));
  int32_t local_offset = 0, dst_offset = 0;
  UErrorCode error;
  // Local time is set to false, and local_offset/dst_offset/error are mutated
  // via non-const references.
  timezone->getOffset(static_cast<UDate>(milliseconds_since_epoch), false,
                      local_offset, dst_offset, error);
  if (error != U_ZERO_ERROR) {
    callback(0, 0);
    return;
  }
  local_offset /= kMillisecondsInMinute;
  dst_offset /= kMillisecondsInMinute;
  callback(local_offset, dst_offset);
}

void TimeServiceImpl::NotifyWatchers(const fidl::String& new_timezone_id) {
  for (auto& watcher : watchers_) {
    watcher->OnTimezoneOffsetChange(new_timezone_id);
  }
}

bool TimeServiceImpl::IsValidTimezoneId(const fidl::String& timezone_id) {
  std::unique_ptr<icu::TimeZone> timezone(
      icu::TimeZone::createTimeZone(timezone_id.get().c_str()));
  if ((*timezone) == icu::TimeZone::getUnknown()) {
    return false;
  }
  return true;
}

void TimeServiceImpl::SetTimezone(const fidl::String& timezone_id,
                                  const SetTimezoneCallback& callback) {
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
  std::ofstream out_fstream(kTzIdFile, std::ofstream::trunc);
  if (!out_fstream.is_open()) {
    FXL_LOG(ERROR) << "Unable to open file for write '" << kTzIdFile << "'";
    callback(false);
    return;
  }
  out_fstream << timezone_id;
  out_fstream.close();
  NotifyWatchers(timezone_id);
  callback(true);
}

void TimeServiceImpl::GetTimezoneId(const GetTimezoneIdCallback& callback) {
  callback(GetTimezoneIdImpl());
}

fidl::String TimeServiceImpl::GetTimezoneIdImpl() {
  if (!valid_) {
    return kDefaultTimezone;
  }
  std::ifstream in_fstream(kTzIdFile);
  if (!in_fstream.is_open()) {
    return kDefaultTimezone;
  }
  std::string id_str;
  in_fstream >> id_str;
  in_fstream.close();

  if (id_str.empty()) {
    FXL_LOG(ERROR) << "TZ file empty at '" << kTzIdFile << "'";
    return kDefaultTimezone;
  }
  if (!IsValidTimezoneId(id_str)) {
    FXL_LOG(ERROR) << "Saved TZ ID invalid: '" << id_str << "'";
    return kDefaultTimezone;
  }
  return id_str;
}

void TimeServiceImpl::ReleaseWatcher(TimeServiceWatcher* watcher) {
  auto predicate = [watcher](const auto& target) {
    return target.get() == watcher;
  };
  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void TimeServiceImpl::Watch(fidl::InterfaceHandle<TimeServiceWatcher> watcher) {
  TimeServiceWatcherPtr watcher_proxy =
      TimeServiceWatcherPtr::Create(std::move(watcher));
  TimeServiceWatcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_connection_error_handler(
      [this, proxy_raw_ptr] { ReleaseWatcher(proxy_raw_ptr); });
  watchers_.push_back(std::move(watcher_proxy));
}

void TimeServiceImpl::AddBinding(fidl::InterfaceRequest<TimeService> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace time_service

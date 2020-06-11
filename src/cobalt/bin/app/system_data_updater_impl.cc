// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/system_data_updater_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdio>
#include <fstream>

namespace cobalt {

using fuchsia::cobalt::Status;

constexpr char kChannelCacheFilenameSuffix[] = "last_reported_channel";
constexpr char kRealmCacheFilenameSuffix[] = "last_reported_realm";

SystemDataUpdaterImpl::SystemDataUpdaterImpl(encoder::SystemDataInterface* system_data,
                                             const std::string& cache_file_name_prefix)
    : system_data_(system_data), cache_file_name_prefix_(cache_file_name_prefix) {
  RestoreData();
}

void SystemDataUpdaterImpl::SetExperimentState(std::vector<fuchsia::cobalt::Experiment> experiments,
                                               SetExperimentStateCallback callback) {
  std::vector<Experiment> experiment_proto_vector;
  for (const auto& experiment_fidl : experiments) {
    Experiment experiment_proto;
    experiment_proto.set_experiment_id(experiment_fidl.experiment_id);
    experiment_proto.set_arm_id(experiment_fidl.arm_id);
    experiment_proto_vector.push_back(experiment_proto);
  }

  system_data_->SetExperimentState(std::move(experiment_proto_vector));
  callback(Status::OK);
}

void SystemDataUpdaterImpl::RestoreData() {
  auto d = Restore(kChannelCacheFilenameSuffix);
  if (!d.empty()) {
    system_data_->SetChannel(d);
  }
  d = Restore(kRealmCacheFilenameSuffix);
  if (!d.empty()) {
    system_data_->SetRealm(d);
  }
}

void SystemDataUpdaterImpl::ClearData() {
  DeleteData(kChannelCacheFilenameSuffix);
  DeleteData(kRealmCacheFilenameSuffix);
}

std::string SystemDataUpdaterImpl::Restore(const std::string& suffix) {
  std::ifstream file(cache_file_name_prefix_ + suffix);
  if (!file) {
    return "";
  }
  std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  FX_LOGS(INFO) << "Restored `" << str << "` from `" << cache_file_name_prefix_ << suffix << "`";
  return str;
}

void SystemDataUpdaterImpl::Persist(const std::string& suffix, const std::string& value) {
  if (value.empty()) {
    return;
  }
  FX_LOGS(INFO) << "Writing `" << value << "` to `" << cache_file_name_prefix_ << suffix << "`";
  std::ofstream c(cache_file_name_prefix_ + suffix);
  c << value;
  c.close();
}

void SystemDataUpdaterImpl::DeleteData(const std::string& suffix) {
  if (std::remove((cache_file_name_prefix_ + suffix).c_str()) == 0) {
    FX_LOGS(INFO) << "Successfully deleted `" << cache_file_name_prefix_ << suffix << "`";
  }
}

void SystemDataUpdaterImpl::SetChannel(std::string current_channel, SetChannelCallback callback) {
  fuchsia::cobalt::SoftwareDistributionInfo current_info;
  current_info.set_current_channel(std::move(current_channel));
  SetSoftwareDistributionInfo(std::move(current_info),
                              [&callback](Status status) { callback(status); });
}

void SystemDataUpdaterImpl::SetSoftwareDistributionInfo(
    fuchsia::cobalt::SoftwareDistributionInfo current_info,
    SetSoftwareDistributionInfoCallback callback) {
  system_data::SoftwareDistributionInfo info;

  if (current_info.has_current_realm()) {
    auto realm = current_info.current_realm();
    Persist(kRealmCacheFilenameSuffix, realm);
    FX_LOGS(INFO) << "Setting realm to `" << realm << "`";
    info.realm = realm;
  }

  if (current_info.has_current_channel()) {
    auto channel = current_info.current_channel();
    Persist(kChannelCacheFilenameSuffix, channel);
    FX_LOGS(INFO) << "Setting channel to `" << channel << "`";
    info.channel = channel;
  }

  system_data_->SetSoftwareDistributionInfo(info);
  callback(Status::OK);
}  // namespace cobalt

}  // namespace cobalt

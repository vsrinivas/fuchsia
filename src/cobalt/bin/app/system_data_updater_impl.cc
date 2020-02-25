// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/system_data_updater_impl.h"

#include <cstdio>
#include <fstream>

#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {

using fuchsia::cobalt::Status;

constexpr char kChannelCacheFilenameSuffix[] = "last_reported_channel";

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
  if (d != "") {
    system_data_->SetChannel(d);
  }
}

void SystemDataUpdaterImpl::ClearData() { DeleteData(kChannelCacheFilenameSuffix); }

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
  if (current_channel == "") {
    system_data_->SetChannel("<unknown>");
  } else {
    Persist(kChannelCacheFilenameSuffix, current_channel);
    system_data_->SetChannel(current_channel);
  }
  callback(Status::OK);
}

}  // namespace cobalt

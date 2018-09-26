// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/system_data_updater_impl.h"

namespace cobalt {

using fuchsia::cobalt::Status;

SystemDataUpdaterImpl::SystemDataUpdaterImpl(encoder::SystemData* system_data)
    : system_data_(system_data) {}

void SystemDataUpdaterImpl::SetExperimentState(
    fidl::VectorPtr<fuchsia::cobalt::Experiment> experiments,
    SetExperimentStateCallback callback) {
  if (experiments.is_null()) {
    callback(Status::INVALID_ARGUMENTS);
  }

  std::vector<Experiment> experiment_proto_vector;
  for (const auto& experiment_fidl : experiments.get()) {
    Experiment experiment_proto;
    experiment_proto.set_experiment_id(experiment_fidl.experiment_id);
    experiment_proto.set_arm_id(experiment_fidl.arm_id);
    experiment_proto_vector.push_back(experiment_proto);
  }

  system_data_->SetExperimentState(std::move(experiment_proto_vector));
  callback(Status::OK);
}

}  // namespace cobalt

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/testapp/cobalt_testapp_encoder.h"

#include "lib/fxl/logging.h"

namespace cobalt {
namespace testapp {

using fidl::VectorPtr;
using fuchsia::cobalt::Status;

bool CobaltTestAppEncoder::EncodeStringAndSend(uint32_t metric_id,
                                               uint32_t encoding_config_id,
                                               std::string val,
                                               bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_string_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddStringObservation(metric_id, encoding_config_id, val,
                                     &status);
    }
    FXL_VLOG(1) << "AddStringObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddStringObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeIntAndSend(uint32_t metric_id,
                                            uint32_t encoding_config_id,
                                            int32_t val,
                                            bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_int_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIntObservation(metric_id, encoding_config_id, val, &status);
    }
    FXL_VLOG(1) << "AddIntObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIntObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeIntDistributionAndSend(
    uint32_t metric_id, uint32_t encoding_config_id,
    std::map<uint32_t, uint64_t> distribution_map, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    VectorPtr<fuchsia::cobalt::BucketDistributionEntry> distribution;
    for (auto it = distribution_map.begin(); distribution_map.end() != it;
         it++) {
      fuchsia::cobalt::BucketDistributionEntry entry;
      entry.index = it->first;
      entry.count = it->second;
      distribution.push_back(std::move(entry));
    }

    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_int_bucket_distribution(std::move(distribution));
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIntBucketDistribution(metric_id, encoding_config_id,
                                         std::move(distribution), &status);
    }
    FXL_VLOG(1) << "AddIntBucketDistribution() => " << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIntBucketDistribution() => "
                     << StatusToString(status);
      return false;
    }
  }

  FXL_LOG(INFO) << "About to Check!";
  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeDoubleAndSend(uint32_t metric_id,
                                               uint32_t encoding_config_id,
                                               double val,
                                               bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_double_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddDoubleObservation(metric_id, encoding_config_id, val,
                                     &status);
    }
    FXL_VLOG(1) << "AddDoubleObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddDoubleObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeIndexAndSend(uint32_t metric_id,
                                              uint32_t encoding_config_id,
                                              uint32_t index,
                                              bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_index_value(index);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIndexObservation(metric_id, encoding_config_id, index,
                                    &status);
    }
    FXL_VLOG(1) << "AddIndexObservation(" << index << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIndexObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeTimerAndSend(
    uint32_t metric_id, uint32_t encoding_config_id, uint32_t start_time,
    uint32_t end_time, std::string timer_id, uint32_t timeout_s,
    bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    encoder_->StartTimer(metric_id, encoding_config_id, timer_id, start_time,
                         timeout_s, &status);
    encoder_->EndTimer(timer_id, end_time, timeout_s, &status);

    FXL_VLOG(1) << "AddTimerObservation("
                << "timer_id:" << timer_id << ", start_time:" << start_time
                << ", end_time:" << end_time << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddTimerObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeMultipartTimerAndSend(
    uint32_t metric_id, std::string part0, uint32_t encoding_id0,
    std::string val0, std::string part1, uint32_t encoding_id1,
    uint32_t start_time, uint32_t end_time, std::string timer_id,
    uint32_t timeout_s, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    VectorPtr<fuchsia::cobalt::ObservationValue> parts(1);
    parts->at(0).name = part0;
    parts->at(0).encoding_id = encoding_id0;
    parts->at(0).value.set_string_value(val0);

    encoder_->StartTimer(metric_id, encoding_id1, timer_id, start_time,
                         timeout_s, &status);
    encoder_->EndTimerMultiPart(timer_id, end_time, part1, std::move(parts),
                                timeout_s, &status);

    FXL_VLOG(1) << "AddMultipartTimerObservation("
                << "timer_id:" << timer_id << ", start_time:" << start_time
                << ", end_time:" << end_time << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddMultipartTimerObservation() => "
                     << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::EncodeStringPairAndSend(
    uint32_t metric_id, std::string part0, uint32_t encoding_id0,
    std::string val0, std::string part1, uint32_t encoding_id1,
    std::string val1, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    VectorPtr<fuchsia::cobalt::ObservationValue> parts(2);
    parts->at(0).name = part0;
    parts->at(0).encoding_id = encoding_id0;
    parts->at(0).value.set_string_value(val0);
    parts->at(1).name = part1;
    parts->at(1).encoding_id = encoding_id1;
    parts->at(1).value.set_string_value(val1);
    encoder_->AddMultipartObservation(metric_id, std::move(parts), &status);
    FXL_VLOG(1) << "AddMultipartObservation(" << val0 << ", " << val1 << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddMultipartObservation() => "
                     << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppEncoder::CheckForSuccessfulSend(bool use_request_send_soon) {
  if (!use_network_) {
    FXL_LOG(INFO) << "Not using the network because --no_network_for_testing "
                     "was passed.";
    return true;
  }

  if (use_request_send_soon) {
    // Use the request-send-soon strategy to check the result of the send.
    bool send_success = false;
    FXL_VLOG(1) << "Invoking RequestSendSoon() now...";
    cobalt_controller_->RequestSendSoon(&send_success);
    FXL_VLOG(1) << "RequestSendSoon => " << send_success;
    return send_success;
  }

  // Use the block-until-empty strategy to check the result of the send.
  FXL_VLOG(1) << "Invoking BlockUntilEmpty(10)...";
  cobalt_controller_->BlockUntilEmpty(10);
  FXL_VLOG(1) << "BlockUntilEmpty() returned.";

  uint32_t num_send_attempts;
  cobalt_controller_->GetNumSendAttempts(&num_send_attempts);
  uint32_t failed_send_attempts;
  cobalt_controller_->GetFailedSendAttempts(&failed_send_attempts);
  FXL_VLOG(1) << "num_send_attempts=" << num_send_attempts;
  FXL_VLOG(1) << "failed_send_attempts=" << failed_send_attempts;
  uint32_t expected_lower_bound = previous_value_of_num_send_attempts_ + 1;
  previous_value_of_num_send_attempts_ = num_send_attempts;
  if (num_send_attempts < expected_lower_bound) {
    FXL_LOG(ERROR) << "num_send_attempts=" << num_send_attempts
                   << " expected_lower_bound=" << expected_lower_bound;
    return false;
  }
  if (failed_send_attempts != 0) {
    FXL_LOG(ERROR) << "failed_send_attempts=" << failed_send_attempts;
    return false;
  }
  return true;
}

std::string StatusToString(fuchsia::cobalt::Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case Status::OBSERVATION_TOO_BIG:
      return "OBSERVATION_TOO_BIG";
    case Status::TEMPORARILY_FULL:
      return "TEMPORARILY_FULL";
    case Status::SEND_FAILED:
      return "SEND_FAILED";
    case Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
}

}  // namespace testapp
}  // namespace cobalt

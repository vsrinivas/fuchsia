// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_ENCODER_H
#define GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_ENCODER_H

#include <map>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>

namespace cobalt {
namespace testapp {

std::string StatusToString(fuchsia::cobalt::Status status);

class CobaltTestAppEncoder {
 public:
  CobaltTestAppEncoder(bool use_network, int num_observations_per_batch,
                       fuchsia::cobalt::ControllerSyncPtr* cobalt_controller)
      : use_network_(use_network),
        num_observations_per_batch_(num_observations_per_batch),
        cobalt_controller_(cobalt_controller) {}

  // Synchronously invokes AddStringObservation() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool EncodeStringAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                           std::string val, bool use_request_send_soon);

  // Synchronously invokes AddIntObservation() |num_observations_per_batch_|
  // times using the given parameters.Then invokes CheckForSuccessfulSend().
  bool EncodeIntAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                        int32_t val, bool use_request_send_soon);

  // Synchronously invokes AddIntBucketDistribution()
  // |num_observations_per_batch_| times using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool EncodeIntDistributionAndSend(
      uint32_t metric_id, uint32_t encoding_config_id,
      std::map<uint32_t, uint64_t> distribution_map,
      bool use_request_send_soon);

  // Synchronously invokes AddDoubleObservation() |num_observations_per_batch_|
  // times using the given parameters.Then invokes CheckForSuccessfulSend().
  bool EncodeDoubleAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                           double val, bool use_request_send_soon);

  // Synchronously invokes AddIndexObservation() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool EncodeIndexAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                          uint32_t index, bool use_request_send_soon);

  // Synchronously invokes AddMultipartObservation() for an observation with
  // two string parts, |num_observations_per_batch_| times, using the given
  // parameters. Then invokes CheckForSuccessfulSend().

  bool EncodeStringPairAndSend(uint32_t metric_id, std::string part0,
                               uint32_t encoding_id0, std::string val0,
                               std::string part1, uint32_t encoding_id1,
                               std::string val1, bool use_request_send_soon);
  bool EncodeTimerAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                          uint32_t start_time, uint32_t end_time,
                          std::string timer_id, uint32_t timeout_s,
                          bool use_request_send_soon);

  bool EncodeMultipartTimerAndSend(uint32_t metric_id, std::string part0,
                                   uint32_t encoding_id0, std::string val0,
                                   std::string part1, uint32_t encoding_id1,
                                   uint32_t start_time, uint32_t end_time,
                                   std::string timer_id, uint32_t timeout_s,
                                   bool use_request_send_soon);

  // If |use_network_| is false this method returns true immediately.
  // Otherwise, uses one of two strategies to cause the Observations that
  // have already been given to the Cobalt Client to be sent to the Shuffler
  // and then checks the status of the send. Returns true just in case the
  // send succeeds.
  //
  // |use_request_send_soon| specifies the strategy. If true then we
  // use the method RequestSendSoon() to ask the Cobalt Client to send the
  // Observations soon and return the status. Otherwise we use the method
  // BlockUntilEmpty() to wait for the CobaltClient to have sent all the
  // Observations it is holding and then we query NumSendAttempts() and
  // FailedSendAttempts().
  bool CheckForSuccessfulSend(bool use_request_send_soon);

  bool use_network_;
  int num_observations_per_batch_;
  int previous_value_of_num_send_attempts_ = 0;

  fuchsia::cobalt::EncoderSyncPtr encoder_;
  fuchsia::cobalt::ControllerSyncPtr* cobalt_controller_;
};

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_ENCODER_H

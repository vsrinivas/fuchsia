// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/client.h"

#include <lib/syslog/cpp/macros.h>

namespace analytics::google_analytics {

namespace {

constexpr char kProtocolVersionKey[] = "v";
constexpr char kProtocolVersion[] = "1";
constexpr char kTrackingIdKey[] = "tid";
constexpr char kClientIdKey[] = "cid";

}  // namespace

Client::Client() : shared_parameters_{{kProtocolVersionKey, kProtocolVersion}} {}

void Client::SetTrackingId(std::string_view tracking_id) {
  shared_parameters_[kTrackingIdKey] = tracking_id;
}

void Client::SetClientId(std::string_view client_id) {
  shared_parameters_[kClientIdKey] = client_id;
}

void Client::AddSharedParameters(const GeneralParameters& shared_parameters) {
  const auto& parameters = shared_parameters.parameters();
  shared_parameters_.insert(parameters.begin(), parameters.end());
}

void Client::AddHit(const Hit& hit) {
  FX_DCHECK(IsReady());

  std::map<std::string, std::string> parameters(shared_parameters_);
  const auto& hit_parameters = hit.parameters();
  parameters.insert(hit_parameters.begin(), hit_parameters.end());

  SendData(user_agent_, std::move(parameters));
}

bool Client::IsReady() const {
  return !(user_agent_.empty() ||
           shared_parameters_.find(kTrackingIdKey) == shared_parameters_.end() ||
           shared_parameters_.find(kClientIdKey) == shared_parameters_.end());
}

}  // namespace analytics::google_analytics

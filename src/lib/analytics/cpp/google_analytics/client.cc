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

fit::promise<void, NetError> Client::AddEvent(const Event& event) const {
  FX_DCHECK(IsReady());

  auto parameters = PrepareParameters();
  auto event_parameters = event.parameters();
  parameters.insert(event_parameters.begin(), event_parameters.end());

  return SendData(user_agent_, parameters);
}

std::map<std::string, std::string> Client::PrepareParameters() const {
  FX_DCHECK(IsReady());

  return {{kProtocolVersionKey, kProtocolVersion},
          {kTrackingIdKey, tracking_id_},
          {kClientIdKey, client_id_}};
}

bool Client::IsReady() const {
  return !(user_agent_.empty() || tracking_id_.empty() || client_id_.empty());
}

}  // namespace analytics::google_analytics

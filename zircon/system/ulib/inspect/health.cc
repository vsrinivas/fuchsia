// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/health.h>
#include <lib/zx/time.h>

namespace inspect {

NodeHealth::NodeHealth(Node* parent_node, const std::function<zx_time_t()>& clock_fn)
    : health_node_(parent_node->CreateChild(kHealthNodeName)),
      health_status_(health_node_.CreateString("status", kHealthStartingUp)),
      timestamp_nanos_(health_node_.CreateInt(kStartTimestamp, clock_fn())) {}

NodeHealth::NodeHealth(Node* parent_node) : NodeHealth(parent_node, zx_clock_get_monotonic) {}

void NodeHealth::Ok() {
  health_message_.reset();
  health_status_.Set(kHealthOk);
}

void NodeHealth::StartingUp() {
  health_message_.reset();
  health_status_.Set(kHealthStartingUp);
}

void NodeHealth::StartingUp(const std::string& message) {
  health_status_.Set(kHealthStartingUp);
  SetMessage(message);
}

void NodeHealth::Unhealthy(const std::string& message) {
  health_status_.Set(kHealthUnhealthy);
  SetMessage(message);
}

void NodeHealth::SetStatus(const std::string& status, const std::string& message) {
  health_status_.Set(status);
  SetMessage(message);
}

void NodeHealth::SetMessage(const std::string& message) {
  if (!health_message_) {
    health_message_ = health_node_.CreateString("message", "");
  }
  health_message_->Set(message);
}

}  // namespace inspect

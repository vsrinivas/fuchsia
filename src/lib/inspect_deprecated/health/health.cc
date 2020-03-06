// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "health.h"

#include <abs_clock/clock.h>

namespace {}  // namespace

namespace inspect_deprecated {

NodeHealth::NodeHealth(Node* parent_node) : NodeHealth(parent_node, abs_clock::RealClock::Get()) {}

NodeHealth::NodeHealth(Node* parent_node, abs_clock::Clock* clock)
    : health_node_(parent_node->CreateChild(kHealthNodeName)),
      health_status_(health_node_.CreateStringProperty("status", kHealthStartingUp)),
      // .get() on zx::time is undocumented, but it looks like the underlying clock is
      // in nanoseconds.
      timestamp_nanos_(health_node_.CreateIntMetric(kStartTimestamp, clock->Now().get())) {}

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
    health_message_ = health_node_.CreateStringProperty("message", "");
  }
  health_message_->Set(message);
}

}  // namespace inspect_deprecated

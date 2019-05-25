// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "health.h"

namespace inspect {

NodeHealth::NodeHealth(Node* parent_node)
    : health_node_(parent_node->CreateChild(kHealthNodeName)),
      health_status_(
          health_node_.CreateStringProperty("status", kHealthStartingUp)) {}

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

void NodeHealth::SetStatus(const std::string& status,
                           const std::string& message) {
  health_status_.Set(status);
  SetMessage(message);
}

void NodeHealth::SetMessage(const std::string& message) {
  if (!health_message_) {
    health_message_ = health_node_.CreateStringProperty("message", "");
  }
  health_message_->Set(message);
}

}  // namespace inspect

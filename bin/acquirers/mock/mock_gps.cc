// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/acquirers/mock/mock_gps.h"

#include <sstream>

#include <fuchsia/modular/cpp/fidl.h>

namespace maxwell {
namespace acquirers {

constexpr char GpsAcquirer::kLabel[];

MockGps::MockGps(fuchsia::modular::ContextEngine* context_engine) {
  fuchsia::modular::ComponentScope scope;
  fuchsia::modular::AgentScope agent_scope;
  agent_scope.url = "MockGps";
  scope.set_agent_scope(std::move(agent_scope));
  context_engine->GetWriter(std::move(scope), writer_.NewRequest());
}

void MockGps::Publish(float latitude, float longitude) {
  std::ostringstream json;
  json << "{ \"lat\": " << latitude << ", \"lng\": " << longitude << " }";
  writer_->WriteEntityTopic(kLabel, json.str());
}

}  // namespace acquirers
}  // namespace maxwell

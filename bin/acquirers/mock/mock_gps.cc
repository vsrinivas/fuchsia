// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/user/scope.fidl.h"

#include "apps/maxwell/src/acquirers/mock/mock_gps.h"

namespace maxwell {
namespace acquirers {

constexpr char GpsAcquirer::kLabel[];

MockGps::MockGps(ContextEngine* context_engine) {
  auto scope = ComponentScope::New();
  auto agent_scope = AgentScope::New();
  agent_scope->url = "MockGps";
  scope->set_agent_scope(std::move(agent_scope));
  context_engine->GetWriter(std::move(scope), writer_.NewRequest());
}

void MockGps::Publish(float latitude, float longitude) {
  std::ostringstream json;
  json << "{ \"lat\": " << latitude << ", \"lng\": " << longitude << " }";
  writer_->WriteEntityTopic(kLabel, json.str());
}

}  // namespace acquirers
}  // namespace maxwell

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/acquirers/mock/mock_gps.h"

namespace maxwell {
namespace acquirers {

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

using namespace maxwell::context_engine;

MockGps::MockGps(const context_engine::ContextEnginePtr& context_engine)
    : ctl_(this) {
  ContextAcquirerClientPtr cx;
  context_engine->RegisterContextAcquirer("MockGps", GetProxy(&cx));

  ContextPublisherControllerPtr ctl_ptr;
  ctl_.Bind(GetProxy(&ctl_ptr));

  cx->Publish(kLabel, kSchema, ctl_ptr.PassInterfaceHandle(), GetProxy(&out_));
}

void MockGps::Publish(float latitude, float longitude) {
  std::ostringstream json;
  json << "{ \"lat\": " << latitude << ", \"lng\": " << longitude << " }";
  out_->Update(json.str());
}

void MockGps::OnHasSubscribers() {
  has_subscribers_ = true;
}

void MockGps::OnNoSubscribers() {
  has_subscribers_ = false;
}

}  // namespace acquirers
}  // namespace maxwell

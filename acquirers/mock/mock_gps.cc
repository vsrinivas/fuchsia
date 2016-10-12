// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/acquirers/mock/mock_gps.h"

#include "mojo/public/cpp/application/connect.h"

namespace maxwell {
namespace acquirers {

using namespace maxwell::context_engine;

MockGps::MockGps(mojo::Shell* shell) : ctl_(this) {
  ContextAcquirerClientPtr cx;
  ConnectToService(shell, "mojo:context_engine", GetProxy(&cx));

  ContextPublisherControllerPtr ctl_ptr;
  ctl_.Bind(GetProxy(&ctl_ptr));

  cx->Publish(kLabel, kSchema, ctl_ptr.PassInterfaceHandle(), GetProxy(&out_));
}

void MockGps::Publish(float latitude, float longitude) {
  std::ostringstream json;
  json << "{ \"lat\": " << latitude << ", \"lng\": " << longitude << " }";
  out_->Update(json.str());
}

}  // namespace acquirers
}  // namespace maxwell

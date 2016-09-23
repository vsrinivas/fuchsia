// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::RunLoop;

using intelligence::ContextPublisherPtr;
using intelligence::PublisherPipePtr;
using intelligence::Status;

#define ONE_MOJO_SECOND   1000000
#define GPS_UPDATE_PERIOD ONE_MOJO_SECOND

class GpsAcquirer : public ApplicationImplBase {
 public:
  void OnInitialize() override {
    srand(time(NULL));

    ContextPublisherPtr cx;
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx));
    cx->StartPublishing("acquirers/gps", GetProxy(&pub_));

    PublishingTick();
  }

 private:
  PublisherPipePtr pub_;

  inline void PublishLocation() {
    std::ostringstream json;
    json << "{ \"longitude\": " << rand() % 18001 / 100. - 90
         << ", \"latitude\": "  << rand() % 36001 / 100. - 180
         << " }";

    pub_->Publish("/location/gps", json.str(), [](Status){});
  }

  // TODO(rosswang): How can we tell when the pipe is closed?
  // TODO(rosswang): Signals from ContextService to indicate whether we should
  // start/stop publishing (whether there are any consumers).
  void PublishingTick() {
    PublishLocation();

    RunLoop::current()->PostDelayedTask(
      [this] { PublishingTick(); },
      GPS_UPDATE_PERIOD);
  }
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  GpsAcquirer app;
  return mojo::RunApplication(request, &app);
}

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
using mojo::Binding;
using mojo::RunLoop;

using namespace intelligence;

#define ONE_MOJO_SECOND 1000000
#define GPS_UPDATE_PERIOD ONE_MOJO_SECOND
#define KEEP_ALIVE_TICKS 3
#define HAS_SUBSCRIBERS -1

class GpsAcquirer : public ApplicationImplBase,
                    public ContextPublisherController {
 public:
  GpsAcquirer() : ctl_(this) {}

  void OnInitialize() override {
    srand(time(NULL));

    ContextAcquirerClientPtr cx;
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx));

    ContextPublisherControllerPtr ctl_ptr;
    ctl_.Bind(GetProxy(&ctl_ptr));

    cx->Publish("/location/gps",
                "https://developers.google.com/maps/documentation/javascript/"
                "3.exp/reference#LatLngLiteral",
                ctl_ptr.PassInterfaceHandle(), GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    if (!tick_keep_alive_) {
      MOJO_LOG(INFO) << "GPS on";
      tick_keep_alive_ = HAS_SUBSCRIBERS;
      PublishingTick();
    } else {
      tick_keep_alive_ = HAS_SUBSCRIBERS;
    }
  }

  void OnNoSubscribers() override {
    tick_keep_alive_ = KEEP_ALIVE_TICKS;
    MOJO_LOG(INFO) << "GPS subscribers lost; keeping GPS on for "
                   << KEEP_ALIVE_TICKS << " seconds";
  }

 private:
  Binding<ContextPublisherController> ctl_;
  ContextPublisherLinkPtr out_;
  int tick_keep_alive_;

  inline void PublishLocation() {
    // For now, this representation must be agreed upon by all parties out of
    // band. In the future, we will want to represent most mathematical typing
    // information in schemas and any remaining semantic information in
    // manifests.
    std::ostringstream json;
    json << "{ \"lat\": " << rand() % 18001 / 100. - 90
         << ", \"lng\": " << rand() % 36001 / 100. - 180 << " }";

    MOJO_LOG(INFO) << "Update by acquirers/gps: " << json.str();

    out_->Update(json.str());
  }

  void PublishingTick() {
    if (tick_keep_alive_ > 0) {
      tick_keep_alive_--;
    }

    PublishLocation();

    if (!tick_keep_alive_) {
      MOJO_LOG(INFO) << "GPS off";
      out_->Update(NULL);
      return;
    }

    RunLoop::current()->PostDelayedTask([this] { PublishingTick(); },
                                        GPS_UPDATE_PERIOD);
  }
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  GpsAcquirer app;
  return mojo::RunApplication(request, &app);
}

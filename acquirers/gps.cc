// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/acquirers/gps.h"

#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/utility/run_loop.h"

#include "apps/maxwell/debug.h"

using maxwell::acquirers::GpsAcquirer;

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

namespace {

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::RunLoop;

using namespace maxwell::context_engine;

#define ONE_MOJO_SECOND 1000000
#define GPS_UPDATE_PERIOD ONE_MOJO_SECOND
#define KEEP_ALIVE_TICKS 3
#define HAS_SUBSCRIBERS -1

class GpsAcquirerImpl : public GpsAcquirer,
                        public maxwell::DebuggableApp,
                        public ContextPublisherController {
 public:
  GpsAcquirerImpl() : ctl_(this) {}

  void OnInitialize() override {
    srand(time(NULL));

    ContextAcquirerClientPtr cx;
    ConnectToService(shell(), "mojo:context_engine", GetProxy(&cx));

    ContextPublisherControllerPtr ctl_ptr;
    ctl_.Bind(GetProxy(&ctl_ptr));

    cx->Publish(kLabel, kSchema, ctl_ptr.PassInterfaceHandle(),
                GetProxy(&out_));
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

  Binding<ContextPublisherController> ctl_;
  ContextPublisherLinkPtr out_;
  int tick_keep_alive_;
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  GpsAcquirerImpl app;
  return mojo::RunApplication(request, &app);
}

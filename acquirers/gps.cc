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
using mojo::InterfaceHandle;
using mojo::RunLoop;

using namespace intelligence;

#define ONE_MOJO_SECOND   1000000
#define GPS_UPDATE_PERIOD ONE_MOJO_SECOND

class GpsAcquirer : public ApplicationImplBase,
                    public ContextPublisherController {
 public:
  GpsAcquirer(): ctl_(this) {}

  void OnInitialize() override {
    srand(time(NULL));

    ContextAcquirerClientPtr cx;
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx));

    ContextPublisherControllerPtr ctl_ptr;
    ctl_.Bind(GetProxy(&ctl_ptr));

    cx->RegisterPublisher("/location/gps", "https://developers.google.com/"
        "maps/documentation/javascript/3.exp/reference#LatLngLiteral",
        ctl_ptr.PassInterfaceHandle());
  }

  void StartPublishing(InterfaceHandle<ContextPublisherLink> link) override {
    out_ = ContextPublisherLinkPtr::Create(link.Pass());
    PublishingTick();
  }

 private:
  Binding<ContextPublisherController> ctl_;
  ContextPublisherLinkPtr out_;

  inline void PublishLocation() {
    // For now, this representation must be agreed upon by all parties out of
    // band. In the future, we will want to represent most mathematical typing
    // information in schemas and any remaining semantic information in
    // manifests.
    std::ostringstream json;
    json << "{ \"lat\": " << rand() % 18001 / 100. - 90
         << ", \"lng\": "  << rand() % 36001 / 100. - 180
         << " }";

    out_->Update(json.str());
  }

  void PublishingTick() {
    if (out_.encountered_error()) {
      out_.reset();
      return;
    }

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

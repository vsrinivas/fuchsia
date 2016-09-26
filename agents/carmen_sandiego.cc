// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <rapidjson/document.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::ServiceProviderPtr;

using namespace intelligence;
using namespace rapidjson;

class CarmenSandiego : public ApplicationImplBase, public ContextListener {
 public:
  CarmenSandiego(): listener_(this) {}

  void OnInitialize() override {
    ContextSubscriberPtr cxin;
    ContextPublisherPtr cxout;

    ServiceProviderPtr service_provider;
    shell()->ConnectToApplication("mojo:context_service",
                                  GetProxy(&service_provider));
    ConnectToService(service_provider.get(), GetProxy(&cxin),
                     ContextSubscriber::Name_);
    ConnectToService(service_provider.get(), GetProxy(&cxout),
                     ContextPublisher::Name_);

    cxout->StartPublishing("agents/carmen_sandiego", GetProxy(&loc_out_));

    ContextListenerPtr listener_ptr;
    listener_.Bind(GetProxy(&listener_ptr));
    cxin->Subscribe("/location/gps", listener_ptr.PassInterfaceHandle());
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from "
                   << update->source << ": "
                   << update->json_value;

    std::string hlloc = "somewhere";

    Document d;
    d.Parse(update->json_value.data());

    if (d.IsObject()) {
      const float latitude = d["latitude"].GetFloat(),
                  longitude = d["longitude"].GetFloat();

      if (latitude > 66) {
        hlloc = "The Arctic";
      } else if (latitude < -66) {
        hlloc = "Antarctica";
      } else if (latitude < 49 && latitude > 25 &&
                 longitude > -125 && longitude < -67) {
        hlloc = "America";
      }
    }

    std::ostringstream json;
    json << "\"" << hlloc << "\"";

    // TODO(rosswang): V0 does not support semantic differentiation by source,
    // so the labels have to be explicitly different. In the future, these could
    // all be refinements on "location"
    loc_out_->Publish("/location/region", json.str());
  }

 private:
  PublisherPipePtr loc_out_;
  Binding<ContextListener> listener_;
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  CarmenSandiego app;
  return mojo::RunApplication(request, &app);
}

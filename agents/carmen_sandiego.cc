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
using mojo::InterfaceHandle;

using namespace intelligence;
using namespace rapidjson;

class CarmenSandiego : public ApplicationImplBase,
                       public ContextPublisherController,
                       public ContextSubscriberLink {
 public:
  CarmenSandiego(): ctl_(this), in_(this) {}

  void OnInitialize() override {
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx_));

    ContextPublisherControllerPtr ctl_ptr;
    ctl_.Bind(GetProxy(&ctl_ptr));
    // TODO(rosswang): V0 does not support semantic differentiation by source,
    // so the labels have to be explicitly different. In the future, these could
    // all be refinements on "location"
    cx_->RegisterPublisher("/location/region", "json:string",
                           ctl_ptr.PassInterfaceHandle());
  }

  void StartPublishing(InterfaceHandle<ContextPublisherLink> link) override {
    out_ = ContextPublisherLinkPtr::Create(link.Pass());

    ContextSubscriberLinkPtr in_ptr;
    in_.Bind(GetProxy(&in_ptr));

    cx_->Subscribe("/location/gps", "https://developers.google.com/maps/"
        "documentation/javascript/3.exp/reference#LatLngLiteral",
        in_ptr.PassInterfaceHandle());

    out_.set_connection_error_handler([this]{
      in_.Unbind();
      out_.reset();
    });
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from "
                   << update->source << ": "
                   << update->json_value;

    std::string hlloc = "somewhere";

    Document d;
    d.Parse(update->json_value.data());

    if (d.IsObject()) {
      const float latitude = d["lat"].GetFloat(),
                  longitude = d["lng"].GetFloat();

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

    out_->Update(json.str());
  }

 private:
  ContextAgentClientPtr cx_;
  Binding<ContextPublisherController> ctl_;
  Binding<ContextSubscriberLink> in_;
  ContextPublisherLinkPtr out_;
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  CarmenSandiego app;
  return mojo::RunApplication(request, &app);
}

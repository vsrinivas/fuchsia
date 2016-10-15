// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <rapidjson/document.h>

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"

#include "apps/maxwell/acquirers/gps.h"
#include "apps/maxwell/debug.h"

using maxwell::acquirers::GpsAcquirer;

constexpr char GpsAcquirer::kLabel[];
constexpr char GpsAcquirer::kSchema[];

namespace {

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::InterfaceHandle;

using namespace maxwell::context_engine;
using namespace rapidjson;

class CarmenSandiego : public maxwell::DebuggableApp,
                       public ContextPublisherController,
                       public ContextSubscriberLink {
 public:
  CarmenSandiego() : ctl_(this), in_(this) {}

  void OnInitialize() override {
    ConnectToService(shell(), "mojo:context_engine", GetProxy(&cx_));

    ContextPublisherControllerPtr ctl_ptr;
    ctl_.Bind(GetProxy(&ctl_ptr));
    // TODO(rosswang): V0 does not support semantic differentiation by source,
    // so the labels have to be explicitly different. In the future, these could
    // all be refinements on "location"
    cx_->Publish("/location/region", "json:string",
                 ctl_ptr.PassInterfaceHandle(), GetProxy(&out_));
  }

  void OnHasSubscribers() override {
    ContextSubscriberLinkPtr in_ptr;
    in_.Bind(GetProxy(&in_ptr));
    cx_->Subscribe(GpsAcquirer::kLabel, GpsAcquirer::kSchema,
                   in_ptr.PassInterfaceHandle());
  }

  void OnNoSubscribers() override {
    in_.Unbind();
    out_->Update(NULL);
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from " << update->source << ": "
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
      } else if (latitude < 49 && latitude > 25 && longitude > -125 &&
                 longitude < -67) {
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

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  CarmenSandiego app;
  return mojo::RunApplication(request, &app);
}

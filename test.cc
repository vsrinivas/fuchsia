// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/context_service/context_service.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"

namespace {

using namespace intelligence;

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::InterfaceHandle;
using mojo::ServiceProvider;

class MaxwellTestApp : public ApplicationImplBase,
                       public ContextSubscriberLink {
 public:
  MaxwellTestApp(): sink_(this) {}

  void OnInitialize() override {
    shell()->ConnectToApplication("mojo:acquirers/gps", GetProxy(&gps_));
    shell()->ConnectToApplication("mojo:agents/carmen_sandiego",
                                  GetProxy(&carmen_sandiego_));

    SuggestionAgentClientPtr cx;
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx));
    ContextSubscriberLinkPtr sink_ptr;
    sink_.Bind(GetProxy(&sink_ptr));
    cx->Subscribe("/location/region", "json:string",
                  sink_ptr.PassInterfaceHandle());
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from "
                   << update->source << ": "
                   << update->json_value;
  }

 private:
  InterfaceHandle<ServiceProvider> gps_, carmen_sandiego_;
  Binding<ContextSubscriberLink> sink_;
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}

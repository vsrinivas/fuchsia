// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace {

using namespace maxwell::context_engine;

using mojo::ApplicationImplBase;
using mojo::Binding;
using mojo::InterfaceHandle;
using mojo::ServiceProvider;
using mojo::RunLoop;

#define ONE_MOJO_SECOND   1000000

class MaxwellTestApp : public ApplicationImplBase,
                       public ContextSubscriberLink {
 public:
  MaxwellTestApp(): sink_(this) {}

  void OnInitialize() override {
    srand(time(NULL));

    shell()->ConnectToApplication("mojo:acquirers/gps", GetProxy(&gps_));
    shell()->ConnectToApplication("mojo:agents/carmen_sandiego",
                                  GetProxy(&carmen_sandiego_));

    ConnectToService(shell(), "mojo:context_engine", GetProxy(&cx_));
    Subscribe();
  }

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate from "
                   << update->source << ": "
                   << update->json_value;
  }

 private:
  InterfaceHandle<ServiceProvider> gps_, carmen_sandiego_;
  SuggestionAgentClientPtr cx_;
  Binding<ContextSubscriberLink> sink_;

  void Subscribe() {
    MOJO_LOG(INFO) << "test subscribing to /location/region for 5 seconds";

    ContextSubscriberLinkPtr sink_ptr;
    sink_.Bind(GetProxy(&sink_ptr));

    cx_->Subscribe("/location/region", "json:string",
                   sink_ptr.PassInterfaceHandle());

    RunLoop::current()->PostDelayedTask([this] { Unsubscribe(); },
                                        5 * ONE_MOJO_SECOND);
  }

  void Unsubscribe() {
    int delay = 1 + rand() % 5;
    MOJO_LOG(INFO) << "test unsubscribing from /location/region for "
                   << delay << " seconds";
    sink_.Close();

    RunLoop::current()->PostDelayedTask([this] { Subscribe(); },
                                        delay * ONE_MOJO_SECOND);
  }
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}

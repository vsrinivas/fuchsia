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

#define ONE_MOJO_SECOND 1000000

class MaxwellTestApp : public ApplicationImplBase {
 public:
  void OnInitialize() override {
    srand(time(NULL));

    shell()->ConnectToApplication("mojo:acquirers/gps", GetProxy(&gps_));
    shell()->ConnectToApplication("mojo:agents/carmen_sandiego",
                                  GetProxy(&carmen_sandiego_));
    shell()->ConnectToApplication("mojo:agents/ideas", GetProxy(&ideas_));
  }

 private:
  InterfaceHandle<ServiceProvider> gps_, carmen_sandiego_, ideas_;
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}

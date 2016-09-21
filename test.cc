// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"

namespace {

using mojo::ApplicationImplBase;
using mojo::InterfaceHandle;
using mojo::ServiceProvider;

class MaxwellTestApp : public ApplicationImplBase {
 public:
  void OnInitialize() override {
    shell()->ConnectToApplication("mojo:acquirers/gps", GetProxy(&gps_));
  }

 private:
  InterfaceHandle<ServiceProvider> gps_;
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}

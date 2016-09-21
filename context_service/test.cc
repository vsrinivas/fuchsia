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
using mojo::ServiceProviderImpl;

using intelligence::ContextPublisherPtr;
using intelligence::PublisherPipePtr;
using intelligence::Status;

class Terminator {
 public:
  void Run(const Status& status) const {
    MOJO_LOG(INFO) << "Status " << status << "; ending test";
    mojo::RunLoop::current()->Quit();
  }
};

class MaxwellTestApp : public ApplicationImplBase {
 public:
  void OnInitialize() override {
    ConnectToService(shell(), "mojo:context_service", GetProxy(&cx_));

    MOJO_LOG(INFO) << "Registering publisher \"test\"";
    cx_->StartPublishing("test", GetProxy(&pub_));

    MOJO_LOG(INFO) << "test << foo: \"bar\"";
    pub_->Publish("foo", "\"bar\"", term_);
  }

 private:
  ContextPublisherPtr cx_;
  // This handle needs to stay alive until the callback has executed. Otherwise,
  // if it goes out of scope and the pipe is closed, the response message never
  // gets back to the callback.
  PublisherPipePtr pub_;
  Terminator term_;
};

} // namespace

MojoResult MojoMain(MojoHandle request) {
  MaxwellTestApp test;
  return mojo::RunApplication(request, &test);
}

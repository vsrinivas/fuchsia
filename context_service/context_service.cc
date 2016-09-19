// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"

namespace {
  using mojo::ApplicationImplBase;
  using mojo::ServiceProviderImpl;

  // The story runner mojo app.
  class ContextServiceApp : public ApplicationImplBase {
   public:
    ContextServiceApp() {}
    ~ContextServiceApp() override {}

    bool OnAcceptConnection(ServiceProviderImpl* const s) override {
      FTL_LOG(INFO) << "TORCHWOOD";

      return true;
    }

   private:
    MOJO_DISALLOW_COPY_AND_ASSIGN(ContextServiceApp);
  };
}

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "BAD WOLF";
  ContextServiceApp app;
  return mojo::RunApplication(request, &app);
}

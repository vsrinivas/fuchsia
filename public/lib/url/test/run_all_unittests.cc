// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <mojo/system/main.h>

#include "gtest/gtest.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/services/icu_data/cpp/icu_data.h"

class TestApp : public mojo::ApplicationImplBase {
 public:
  TestApp() {}

  void OnInitialize() override {
    mojo::ApplicationConnectorPtr application_connector;
    shell()->CreateApplicationConnector(mojo::GetProxy(&application_connector));

    bool icu_success = icu_data::Initialize(application_connector.get());
    FTL_DCHECK(icu_success);

    // InitGoogleTest needs an argv.
    std::array<const char *, 1> fake_args({{
        "test", }});
    int argc = fake_args.size();
    ::testing::InitGoogleTest(&argc, const_cast<char **>(fake_args.data()));

    int result = RUN_ALL_TESTS();

    printf("RUN_ALL_TESTS() returned %d", result);

    icu_success = icu_data::Release();
    FTL_DCHECK(icu_success);

    exit(result);
  }

 private:
  MOJO_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

MojoResult MojoMain(MojoHandle application_request) {
  TestApp app;
  return mojo::RunApplication(application_request, &app);
}

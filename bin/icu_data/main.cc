// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <utility>

#include "apps/icu_data/icu_data_provider_impl.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/system/macros.h"

namespace icu_data {

class App : public mojo::ApplicationImplBase {
public:
  App() = default;
  ~App() override = default;

private:
  // |ApplicationImplBase| override:
  void OnInitialize() override {
    if (!icu_data_.LoadData())
      mojo::TerminateApplication(MOJO_RESULT_UNAVAILABLE);
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl *service_provider_impl) override {
    service_provider_impl->AddService<mojo::ICUDataProvider>(
        [this](const mojo::ConnectionContext &connection_context,
               mojo::InterfaceRequest<mojo::ICUDataProvider> request) {
          icu_data_.AddBinding(std::move(request));
        });
    return true;
  }

  ICUDataProviderImpl icu_data_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(App);
};

} // namespace icu_data

MojoResult MojoMain(MojoHandle request) {
  icu_data::App app;
  return mojo::RunApplication(request, &app);
}

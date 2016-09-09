// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <utility>

#include "apps/fonts/font_provider_impl.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/system/macros.h"

namespace fonts {

class App : public mojo::ApplicationImplBase {
 public:
  App() = default;
  ~App() override = default;

 private:
  // |ApplicationImplBase| override:
  void OnInitialize() override {
    if (!font_provider_.LoadFonts())
      mojo::TerminateApplication(MOJO_RESULT_UNAVAILABLE);
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<mojo::FontProvider>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<mojo::FontProvider> request) {
          font_provider_.AddBinding(std::move(request));
        });
    return true;
  }

  FontProviderImpl font_provider_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace fonts

MojoResult MojoMain(MojoHandle request) {
  fonts::App app;
  return mojo::RunApplication(request, &app);
}

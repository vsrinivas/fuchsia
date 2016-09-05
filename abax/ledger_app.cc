// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <mojo/system/handle.h>
#include <mojo/system/main.h>

#include "apps/ledger/abax/ledger_factory_impl.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connection_context.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace ledger {

class LedgerApp : public mojo::ApplicationImplBase {
 public:
  LedgerApp() {}
  ~LedgerApp() override {}

 private:
  void OnInitialize() override {}

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<LedgerFactory>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<LedgerFactory> ledger_request) {
          new LedgerFactoryImpl(std::move(ledger_request));
        });
    return true;
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerApp);
};

}  // namespace ledger

MojoResult MojoMain(MojoHandle application_request) {
  ledger::LedgerApp app;
  return mojo::RunApplication(application_request, &app);
}

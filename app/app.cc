// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <mojo/system/handle.h>
#include <mojo/system/main.h>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/ledger_factory_impl.h"
#include "apps/network/interfaces/network_service.mojom.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connection_context.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace ledger {

namespace {

const char kStoragePathArg[] = "--storage-path=";
const size_t kStoragePathArgLength = sizeof(kStoragePathArg) - 1;

const char kDefaultStoragePath[] = "/data/ledger";

}  // namespace

// App is the main entry point of the Ledger Mojo application.
//
// It is responsible for setting up the LedgerFactory, which connects clients to
// individual ledger instances. It should not however hold long-lived objects
// shared between ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App : public mojo::ApplicationImplBase {
 public:
  App() {}
  ~App() override {}

 private:
  void OnInitialize() override {
    storage_path_ = kDefaultStoragePath;

    for (const std::string& arg : args()) {
      if (arg.size() > kStoragePathArgLength &&
          arg.substr(0, kStoragePathArgLength) == kStoragePathArg) {
        storage_path_ = arg.substr(kStoragePathArgLength);
        break;
      }
    }

    if (!files::IsDirectory(storage_path_) &&
        !files::CreateDirectory(storage_path_)) {
      FTL_LOG(ERROR) << "Unable to access " << storage_path_;
      Terminate(MOJO_RESULT_PERMISSION_DENIED);
      return;
    }

    factory_impl_.reset(new LedgerFactoryImpl(
        mtl::MessageLoop::GetCurrent()->task_runner(), storage_path_));
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<LedgerFactory>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<LedgerFactory> factory_request) {
          factory_bindings_.AddBinding(factory_impl_.get(),
                                       std::move(factory_request));
        });
    return true;
  }

  std::string storage_path_;
  std::unique_ptr<LedgerFactoryImpl> factory_impl_;
  mojo::BindingSet<LedgerFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ledger

MojoResult MojoMain(MojoHandle application_request) {
  ledger::App app;
  return mojo::RunApplication(application_request, &app);
}

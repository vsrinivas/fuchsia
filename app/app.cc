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
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connection_context.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace ledger {

namespace {

const char kStorageArg[] = "--storage_path=";
const size_t kStorageArgLength = sizeof(kStorageArg) - 1;

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
    message_loop_ = mtl::MessageLoop::GetCurrent();

    bool storage_path_given = false;
    for (const std::string& arg : args()) {
      if (arg.size() > kStorageArgLength &&
          arg.substr(0, kStorageArgLength) == kStorageArg) {
        std::string storage_path = arg.substr(kStorageArgLength);
        storage_path_ = storage_path;
        storage_path_given = true;
        break;
      }
    }
    if (!storage_path_given) {
      temp_storage_.reset(new files::ScopedTempDir());
      storage_path_ = temp_storage_->path();
    }
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<LedgerFactory>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<LedgerFactory> ledger_request) {
          new LedgerFactoryImpl(std::move(ledger_request),
                                message_loop_->task_runner(), storage_path_);
        });
    return true;
  }

  std::string storage_path_;
  std::unique_ptr<files::ScopedTempDir> temp_storage_;
  mtl::MessageLoop* message_loop_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ledger

MojoResult MojoMain(MojoHandle application_request) {
  ledger::App app;
  return mojo::RunApplication(application_request, &app);
}

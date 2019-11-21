// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/e2e_sync/ledger_app_instance_factory_e2e.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/optional.h>

#include <utility>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl_helpers/bound_interface_set.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/firebase_auth/testing/fake_token_manager.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {
constexpr fxl::StringView kLedgerName = "AppTests";

class LedgerAppInstanceImpl final : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(LoopController* loop_controller, rng::Random* random,
                        ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
                        fuchsia::inspect::deprecated::InspectPtr inspect, SyncParams sync_params,
                        cloud_provider_firestore::CloudProviderFactory::UserId user_id);

  void Init(
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request,
      fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request);

 private:
  cloud_provider::CloudProviderPtr MakeCloudProvider() override;
  std::string GetUserId() override;

  std::unique_ptr<sys::ComponentContext> component_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;

  fuchsia::sys::ComponentControllerPtr controller_;
  const cloud_provider_firestore::CloudProviderFactory::UserId user_id_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    LoopController* loop_controller, rng::Random* random,
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
    fuchsia::inspect::deprecated::InspectPtr inspect, SyncParams sync_params,
    cloud_provider_firestore::CloudProviderFactory::UserId user_id)
    : LedgerAppInstanceFactory::LedgerAppInstance(loop_controller, convert::ToArray(kLedgerName),
                                                  std::move(ledger_repository_factory),
                                                  std::move(inspect)),
      component_context_(sys::ComponentContext::Create()),
      cloud_provider_factory_(component_context_.get(), random, std::move(sync_params.api_key),
                              std::move(sync_params.credentials)),
      user_id_(std::move(user_id)) {}

void LedgerAppInstanceImpl::Init(
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request,
    fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request) {
  cloud_provider_factory_.Init();

  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=12278): Connect |inspect_request| to
  // the Ledger component under test.
  fidl::InterfaceHandle<fuchsia::io::Directory> child_directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/ledger#meta/ledger.cmx";
  launch_info.directory_request = child_directory.NewRequest().TakeChannel();
  *launch_info.arguments = {"--disable_reporting", "--disable_p2p_sync"};
  ledger::AppendGarbageCollectionPolicyFlags(kTestingGarbageCollectionPolicy, &launch_info);
  fuchsia::sys::LauncherPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  sys::ServiceDirectory child_services(std::move(child_directory));
  child_services.Connect(std::move(repository_factory_request));
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider.NewRequest());
  return cloud_provider;
}

std::string LedgerAppInstanceImpl::GetUserId() { return user_id_.user_id(); }

}  // namespace

LedgerAppInstanceFactoryImpl::LedgerAppInstanceFactoryImpl(
    std::unique_ptr<LoopController> loop_controller, SyncParams sync_params)
    : loop_controller_(std::move(loop_controller)),
      sync_params_(std::move(sync_params)),
      user_id_(cloud_provider_firestore::CloudProviderFactory::UserId::New()) {}

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() = default;

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request =
      repository_factory.NewRequest();
  fuchsia::inspect::deprecated::InspectPtr inspect;
  fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request =
      inspect.NewRequest();
  auto result = std::make_unique<LedgerAppInstanceImpl>(loop_controller_.get(), &random_,
                                                        std::move(repository_factory),
                                                        std::move(inspect), sync_params_, user_id_);
  result->Init(std::move(repository_factory_request), std::move(inspect_request));
  return result;
}

LoopController* LedgerAppInstanceFactoryImpl::GetLoopController() { return loop_controller_.get(); }

rng::Random* LedgerAppInstanceFactoryImpl::GetRandom() { return &random_; }

}  // namespace ledger

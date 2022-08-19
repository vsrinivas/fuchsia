// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/examples/services/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/service/cpp/service.h>
#include <lib/sys/service/cpp/service_aggregate.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

class BankAccountTest : public ::gtest::RealLoopFixture {
 public:
  explicit BankAccountTest() {
    auto context = sys::ComponentContext::Create();
    // Connect to the fuchsia.component.Realm framework protocol
    context->svc()->Connect(realm_proxy_.NewRequest());
    // Return a channel connected to the /svc directory.
    svc_ = context->svc()->CloneChannel();
  }

  // Create and instance of the provider component and return its exposed directory
  fuchsia::io::DirectorySyncPtr StartProvider(const char* component_name,
                                              const char* component_url) {
    FX_SLOG(INFO, "Creating BankAccount provider", KV("url", component_url),
            KV("name", component_name));
    fuchsia::component::decl::CollectionRef collection_ref = {
        .name = "account_providers",
    };
    fuchsia::component::decl::Child child_decl;
    child_decl.set_name(component_name);
    child_decl.set_url(component_url);
    child_decl.set_startup(fuchsia::component::decl::StartupMode::LAZY);

    fuchsia::component::Realm_CreateChild_Result create_result;
    realm_proxy_->CreateChild(std::move(collection_ref), std::move(child_decl),
                              fuchsia::component::CreateChildArgs(), &create_result);

    FX_SLOG(INFO, "Open exposed dir of BankAccount provider", KV("url", component_url),
            KV("name", component_name));
    fuchsia::component::decl::ChildRef child_ref = {
        .name = component_name,
        .collection = "account_providers",
    };
    fuchsia::io::DirectorySyncPtr exposed_dir;
    fuchsia::component::Realm_OpenExposedDir_Result open_result;
    realm_proxy_->OpenExposedDir(child_ref, exposed_dir.NewRequest(), &open_result);

    return exposed_dir;
  }

 protected:
  // TODO(https://fxbug.dev/101092): Use default namespace to connect services.
  fidl::InterfaceHandle<fuchsia::io::Directory> svc_;

 private:
  fuchsia::component::RealmSyncPtr realm_proxy_;
};

TEST_F(BankAccountTest, ReadWriteMultipleServiceInstances) {
  // Launch two BankAccount providers into the `account_providers` collection.
  StartProvider("a", "#meta/provider-a.cm");
  StartProvider("b", "#meta/provider-b.cm");

  // List available instances of the BankAccount service
  auto service_aggregate =
      sys::OpenServiceAggregateAt<fuchsia::examples::services::BankAccount>(svc_);
  auto instance_names = service_aggregate.ListInstances();
  ZX_ASSERT(!instance_names.empty());

  // Debit both bank accounts by $5.
  for (auto& instance : instance_names) {
    auto service = sys::OpenServiceAt<fuchsia::examples::services::BankAccount>(svc_, instance);
    // Connect to the ReadOnlyAccount protocol offered by this service
    fuchsia::examples::services::ReadOnlyAccountSyncPtr read_only_account =
        service.read_only().Connect().BindSync();
    std::string initial_owner;
    read_only_account->GetOwner(&initial_owner);
    int64_t initial_balance;
    read_only_account->GetBalance(&initial_balance);
    FX_LOGS(INFO) << "Retrieved account: " << initial_owner;

    // Connect to the ReadWriteAccount protocol offered by this service
    fuchsia::examples::services::ReadWriteAccountSyncPtr read_write_account =
        service.read_write().Connect().BindSync();
    std::string owner;
    read_write_account->GetOwner(&owner);
    ASSERT_EQ(initial_owner, owner);
    int64_t balance;
    read_write_account->GetBalance(&balance);
    ASSERT_EQ(initial_balance, balance);

    FX_LOGS(INFO) << "Debiting account: " << owner;
    bool success;
    read_write_account->Debit(5, &success);
    ASSERT_TRUE(success);
    read_write_account->GetBalance(&balance);
    ASSERT_EQ(initial_balance - 5, balance);
  }
}

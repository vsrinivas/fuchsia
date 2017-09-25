// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/ledger_app_instance_factory.h"

#include "gtest/gtest.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace test {
namespace {
constexpr fxl::TimeDelta kTimeout = fxl::TimeDelta::FromSeconds(10);
}

LedgerAppInstanceFactory::LedgerAppInstance::LedgerAppInstance(
    ledger::FirebaseConfigPtr firebase_config,
    fidl::Array<uint8_t> test_ledger_name,
    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory,
    fxl::RefPtr<fxl::TaskRunner> services_task_runner)
    : firebase_config_(std::move(firebase_config)),
      test_ledger_name_(std::move(test_ledger_name)),
      ledger_repository_factory_(std::move(ledger_repository_factory)),
      token_provider_impl_("",
                           "sync_user",
                           "sync_user@google.com",
                           "client_id"),
      services_task_runner_(std::move(services_task_runner)) {}

LedgerAppInstanceFactory::LedgerAppInstance::~LedgerAppInstance() {
  UnbindTokenProvider();
}

ledger::LedgerRepositoryFactory*
LedgerAppInstanceFactory::LedgerAppInstance::ledger_repository_factory() {
  return ledger_repository_factory_.get();
}

ledger::LedgerRepositoryPtr
LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedgerRepository() {
  ledger::LedgerRepositoryPtr repository;

  modular::auth::TokenProviderPtr token_provider;
  EXPECT_TRUE(callback::RunSynchronously(
      services_task_runner_, fxl::MakeCopyable([
        this, request = token_provider.NewRequest()
      ]() mutable { token_provider_impl_.AddBinding(std::move(request)); }),
      kTimeout));

  ledger::Status status;
  ledger_repository_factory_->GetRepository(
      dir_.path(), firebase_config_.Clone(), std::move(token_provider),
      repository.NewRequest(), [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(
      ledger_repository_factory_.WaitForIncomingResponseWithTimeout(kTimeout));
  EXPECT_EQ(ledger::Status::OK, status);
  return repository;
}

void LedgerAppInstanceFactory::LedgerAppInstance::EraseTestLedgerRepository() {
  modular::auth::TokenProviderPtr token_provider;
  EXPECT_TRUE(callback::RunSynchronously(
      services_task_runner_, fxl::MakeCopyable([
        this, request = token_provider.NewRequest()
      ]() mutable { token_provider_impl_.AddBinding(std::move(request)); }),
      kTimeout));

  ledger::Status status;
  ledger_repository_factory_->EraseRepository(
      dir_.path(), firebase_config_.Clone(), std::move(token_provider),
      [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(
      ledger_repository_factory_.WaitForIncomingResponseWithTimeout(kTimeout));
  EXPECT_EQ(ledger::Status::OK, status);
}

ledger::LedgerPtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedger() {
  ledger::LedgerPtr ledger;

  ledger::LedgerRepositoryPtr repository = GetTestLedgerRepository();
  ledger::Status status;
  repository->GetLedger(test_ledger_name_.Clone(), ledger.NewRequest(),
                        [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponseWithTimeout(kTimeout));
  EXPECT_EQ(ledger::Status::OK, status);
  return ledger;
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestPage() {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->GetPage(nullptr, page.NewRequest(),
                  [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger.WaitForIncomingResponseWithTimeout(
      fxl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(ledger::Status::OK, status);

  return fidl::InterfacePtr<ledger::Page>::Create(std::move(page));
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetPage(
    const fidl::Array<uint8_t>& page_id,
    ledger::Status expected_status) {
  ledger::PagePtr page_ptr;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->GetPage(page_id.Clone(), page_ptr.NewRequest(),
                  [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger.WaitForIncomingResponseWithTimeout(
      fxl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void LedgerAppInstanceFactory::LedgerAppInstance::DeletePage(
    const fidl::Array<uint8_t>& page_id,
    ledger::Status expected_status) {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->DeletePage(page_id.Clone(),
                     [&status](ledger::Status s) { status = s; });
  EXPECT_TRUE(ledger.WaitForIncomingResponseWithTimeout(
      fxl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);
}

void LedgerAppInstanceFactory::LedgerAppInstance::UnbindTokenProvider() {
  ASSERT_TRUE(callback::RunSynchronously(
      services_task_runner_,
      [this] { token_provider_impl_.CloseAllBindings(); },
      fxl::TimeDelta::FromSeconds(1)));
}

}  // namespace test

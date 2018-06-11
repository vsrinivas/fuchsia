// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/zx/time.h>

#include "garnet/public/lib/callback/capture.h"
#include "gtest/gtest.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/lib/convert/convert.h"

namespace test {

LedgerAppInstanceFactory::LedgerAppInstance::LedgerAppInstance(
    LoopController* loop_controller, fidl::VectorPtr<uint8_t> test_ledger_name,
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory)
    : loop_controller_(loop_controller),
      test_ledger_name_(std::move(test_ledger_name)),
      ledger_repository_factory_(std::move(ledger_repository_factory)) {}

LedgerAppInstanceFactory::LedgerAppInstance::~LedgerAppInstance() {}

ledger_internal::LedgerRepositoryFactory*
LedgerAppInstanceFactory::LedgerAppInstance::ledger_repository_factory() {
  return ledger_repository_factory_.get();
}

ledger_internal::LedgerRepositoryPtr
LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedgerRepository() {
  ledger_internal::LedgerRepositoryPtr repository;
  ledger::Status status;
  ledger_repository_factory_->GetRepository(
      dir_.path(), MakeCloudProvider(), repository.NewRequest(),
      callback::Capture([this] { loop_controller_->StopLoop(); }, &status));
  loop_controller_->RunLoop();
  EXPECT_EQ(ledger::Status::OK, status);
  return repository;
}

ledger::LedgerPtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedger() {
  ledger::LedgerPtr ledger;

  ledger_internal::LedgerRepositoryPtr repository = GetTestLedgerRepository();
  ledger::Status status;
  repository->GetLedger(
      fidl::Clone(test_ledger_name_), ledger.NewRequest(),
      callback::Capture([this] { loop_controller_->StopLoop(); }, &status));
  loop_controller_->RunLoop();
  EXPECT_EQ(ledger::Status::OK, status);
  return ledger;
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestPage() {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->GetPage(
      nullptr, page.NewRequest(),
      callback::Capture([this] { loop_controller_->StopLoop(); }, &status));
  loop_controller_->RunLoop();
  EXPECT_EQ(ledger::Status::OK, status);

  return page.Bind();
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetPage(
    const ledger::PageIdPtr& page_id, ledger::Status expected_status) {
  ledger::PagePtr page_ptr;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->GetPage(
      fidl::Clone(page_id), page_ptr.NewRequest(),
      callback::Capture([this] { loop_controller_->StopLoop(); }, &status));
  loop_controller_->RunLoop();
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void LedgerAppInstanceFactory::LedgerAppInstance::DeletePage(
    const ledger::PageId& page_id, ledger::Status expected_status) {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  ledger->DeletePage(
      fidl::Clone(page_id),
      callback::Capture([this] { loop_controller_->StopLoop(); }, &status));
  loop_controller_->RunLoop();
  EXPECT_EQ(expected_status, status);
}

}  // namespace test

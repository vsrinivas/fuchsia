// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_app_instance_factory.h"

#include <lib/fidl/cpp/clone.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

LedgerAppInstanceFactory::LedgerAppInstance::LedgerAppInstance(
    LoopController* loop_controller, std::vector<uint8_t> test_ledger_name,
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
    fuchsia::inspect::deprecated::InspectPtr inspect)
    : loop_controller_(loop_controller),
      test_ledger_name_(std::move(test_ledger_name)),
      ledger_repository_factory_(std::move(ledger_repository_factory)),
      inspect_(std::move(inspect)),
      platform_(MakePlatform()),
      tmp_location_(platform_->file_system()->CreateScopedTmpLocation()) {
  ledger_repository_factory_.set_error_handler([](zx_status_t status) {
    if (status != ZX_ERR_PEER_CLOSED) {
      ADD_FAILURE() << "|LedgerRepositoryFactory| failed with an error: " << status;
    }
  });
}

LedgerAppInstanceFactory::LedgerAppInstance::~LedgerAppInstance() = default;

ledger_internal::LedgerRepositoryFactory*
LedgerAppInstanceFactory::LedgerAppInstance::ledger_repository_factory() {
  return ledger_repository_factory_.get();
}

ledger_internal::LedgerRepositoryPtr
LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedgerRepository() {
  ledger_internal::LedgerRepositoryPtr repository;
  repository.set_error_handler([](zx_status_t status) {
    if (status != ZX_ERR_PEER_CLOSED) {
      ADD_FAILURE() << "|LedgerRepository| failed with an error: " << status;
    }
  });
  ledger_repository_factory_->GetRepository(
      fsl::CloneChannelFromFileDescriptor(tmp_location_->path().root_fd()), MakeCloudProvider(),
      GetUserId(), repository.NewRequest());
  return repository;
}

LedgerPtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedger() {
  LedgerPtr ledger;
  ledger.set_error_handler([](zx_status_t status) {
    if (status != ZX_ERR_PEER_CLOSED) {
      ADD_FAILURE() << "|Ledger| failed with an error: " << status;
    }
  });

  auto repository = GetTestLedgerRepository();
  repository->GetLedger(fidl::Clone(test_ledger_name_), ledger.NewRequest());
  auto waiter = loop_controller_->NewWaiter();
  repository->Sync(waiter->GetCallback());
  if (!waiter->RunUntilCalled()) {
    ADD_FAILURE() << "|GetLedger| failed to call back.";
    return nullptr;
  }
  return ledger;
}

PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestPage() {
  PagePtr page;
  GetTestLedger()->GetPage(nullptr, page.NewRequest());
  return page;
}

PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetPage(const PageIdPtr& page_id) {
  PagePtr page_ptr;
  GetTestLedger()->GetPage(fidl::Clone(page_id), page_ptr.NewRequest());
  return page_ptr;
}

bool LedgerAppInstanceFactory::LedgerAppInstance::Inspect(
    LoopController* loop_controller, inspect_deprecated::ObjectHierarchy* hierarchy) {
  return ledger::Inspect(&inspect_, loop_controller, hierarchy);
}

}  // namespace ledger

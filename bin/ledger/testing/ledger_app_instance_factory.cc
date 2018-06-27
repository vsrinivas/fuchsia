// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include "garnet/public/lib/callback/capture.h"
#include "gtest/gtest.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace {
class CallbackWaiterImpl : public LedgerAppInstanceFactory::CallbackWaiter {
 public:
  CallbackWaiterImpl(LedgerAppInstanceFactory::LoopController* loop_controller)
      : loop_controller_(loop_controller) {}
  CallbackWaiterImpl(const CallbackWaiterImpl&) = delete;
  CallbackWaiterImpl& operator=(const CallbackWaiterImpl&) = delete;
  virtual ~CallbackWaiterImpl() = default;

  fit::function<void()> GetCallback() override {
    return [this] {
      ++callback_called_;
      if (waiting_) {
        loop_controller_->StopLoop();
      }
    };
  }

  void RunUntilCalled() override {
    FXL_DCHECK(!waiting_);
    waiting_ = true;
    while (NotCalledYet()) {
      loop_controller_->RunLoop();
    }
    waiting_ = false;
    ++run_until_called_;
  }

  bool NotCalledYet() override { return callback_called_ <= run_until_called_; }

 private:
  LedgerAppInstanceFactory::LoopController* loop_controller_;
  size_t callback_called_ = 0;
  size_t run_until_called_ = 0;
  bool waiting_ = false;
};
}  // namespace

std::unique_ptr<LedgerAppInstanceFactory::CallbackWaiter>
LedgerAppInstanceFactory::LoopController::NewWaiter() {
  return std::make_unique<CallbackWaiterImpl>(this);
}

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
  auto waiter = loop_controller_->NewWaiter();
  ledger_repository_factory_->GetRepository(
      dir_.path(), MakeCloudProvider(), repository.NewRequest(),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  return repository;
}

ledger::LedgerPtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestLedger() {
  ledger::LedgerPtr ledger;

  ledger_internal::LedgerRepositoryPtr repository = GetTestLedgerRepository();
  ledger::Status status;
  auto waiter = loop_controller_->NewWaiter();
  repository->GetLedger(fidl::Clone(test_ledger_name_), ledger.NewRequest(),
                        callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);
  return ledger;
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetTestPage() {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  auto waiter = loop_controller_->NewWaiter();
  ledger->GetPage(nullptr, page.NewRequest(),
                  callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(ledger::Status::OK, status);

  return page.Bind();
}

ledger::PagePtr LedgerAppInstanceFactory::LedgerAppInstance::GetPage(
    const ledger::PageIdPtr& page_id, ledger::Status expected_status) {
  ledger::PagePtr page_ptr;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  auto waiter = loop_controller_->NewWaiter();
  ledger->GetPage(fidl::Clone(page_id), page_ptr.NewRequest(),
                  callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void LedgerAppInstanceFactory::LedgerAppInstance::DeletePage(
    const ledger::PageId& page_id, ledger::Status expected_status) {
  fidl::InterfaceHandle<ledger::Page> page;
  ledger::Status status;
  ledger::LedgerPtr ledger = GetTestLedger();
  auto waiter = loop_controller_->NewWaiter();
  ledger->DeletePage(fidl::Clone(page_id),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(expected_status, status);
}

}  // namespace test

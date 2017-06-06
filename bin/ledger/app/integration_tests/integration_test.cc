// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/integration_tests/integration_test.h"

#include "apps/ledger/src/app/integration_tests/test_utils.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/glue/socket/socket_writer.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace ledger {
namespace integration_tests {

void IntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  thread_ = mtl::CreateThread(&task_runner_);
  task_runner_->PostTask(ftl::MakeCopyable(
      [ this, request = ledger_repository_factory_.NewRequest() ]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            task_runner_, tmp_dir_.path(), std::move(request));
      }));
  socket_thread_ = mtl::CreateThread(&socket_task_runner_);
  ledger_ = GetTestLedger();
  std::srand(0);
}

void IntegrationTest::TearDown() {
  task_runner_->PostTask([this]() {
    mtl::MessageLoop::GetCurrent()->QuitNow();
    factory_container_.reset();
  });
  thread_.join();

  socket_task_runner_->PostTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  socket_thread_.join();

  ::testing::Test::TearDown();
}

mx::socket IntegrationTest::StreamDataToSocket(std::string data) {
  glue::SocketPair sockets;
  socket_task_runner_->PostTask(ftl::MakeCopyable([
    socket = std::move(sockets.socket1), data = std::move(data)
  ]() mutable {
    auto writer = new glue::StringSocketWriter();
    writer->Start(std::move(data), std::move(socket));
  }));
  return std::move(sockets.socket2);
}

LedgerPtr IntegrationTest::GetTestLedger() {
  Status status;
  LedgerRepositoryPtr repository;
  ledger_repository_factory_->GetRepository(
      tmp_dir_.path(), nullptr, nullptr, repository.NewRequest(),
      [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_repository_factory_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(Status::OK, status);

  LedgerPtr ledger;
  repository->GetLedger(RandomArray(1), ledger.NewRequest(),
                        [&status](Status s) { status = s; });
  EXPECT_TRUE(repository.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(Status::OK, status);
  return ledger;
}

PagePtr IntegrationTest::GetTestPage() {
  fidl::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(nullptr, page.NewRequest(),
                   [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(Status::OK, status);

  return fidl::InterfacePtr<Page>::Create(std::move(page));
}

PagePtr IntegrationTest::GetPage(const fidl::Array<uint8_t>& page_id,
                                 Status expected_status) {
  PagePtr page_ptr;
  Status status;

  ledger_->GetPage(page_id.Clone(), page_ptr.NewRequest(),
                   [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);

  return page_ptr;
}

void IntegrationTest::DeletePage(const fidl::Array<uint8_t>& page_id,
                                 Status expected_status) {
  fidl::InterfaceHandle<Page> page;
  Status status;

  ledger_->DeletePage(page_id.Clone(), [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(expected_status, status);
}

}  // namespace integration_tests
}  // namespace ledger

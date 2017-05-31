// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/integration_tests/test_utils.h"

#include <string>
#include <utility>
#include <vector>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/glue/socket/socket_writer.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace integration_tests {

fidl::Array<uint8_t> RandomArray(size_t size,
                                 const std::vector<uint8_t>& prefix) {
  EXPECT_TRUE(size >= prefix.size());
  fidl::Array<uint8_t> array = fidl::Array<uint8_t>::New(size);
  for (size_t i = 0; i < prefix.size(); ++i) {
    array[i] = prefix[i];
  }
  for (size_t i = prefix.size(); i < size / 4; ++i) {
    int random = std::rand();
    for (size_t j = 0; j < 4 && 4 * i + j < size; ++j) {
      array[4 * i + j] = random & 0xFF;
      random = random >> 8;
    }
  }
  return array;
}

fidl::Array<uint8_t> RandomArray(int size) {
  return RandomArray(size, std::vector<uint8_t>());
}

fidl::Array<uint8_t> PageGetId(PagePtr* page) {
  fidl::Array<uint8_t> page_id;
  (*page)->GetId(
      [&page_id](fidl::Array<uint8_t> id) { page_id = std::move(id); });
  EXPECT_TRUE(
      page->WaitForIncomingResponseWithTimeout(ftl::TimeDelta::FromSeconds(1)));
  return page_id;
}

PageSnapshotPtr PageGetSnapshot(PagePtr* page, fidl::Array<uint8_t> prefix) {
  PageSnapshotPtr snapshot;
  (*page)->GetSnapshot(snapshot.NewRequest(), std::move(prefix), nullptr,
                       [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(
      page->WaitForIncomingResponseWithTimeout(ftl::TimeDelta::FromSeconds(1)));
  return snapshot;
}

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start) {
  return SnapshotGetKeys(snapshot, std::move(start), nullptr);
}

fidl::Array<fidl::Array<uint8_t>> SnapshotGetKeys(PageSnapshotPtr* snapshot,
                                                  fidl::Array<uint8_t> start,
                                                  int* num_queries) {
  fidl::Array<fidl::Array<uint8_t>> result;
  fidl::Array<uint8_t> next_token = nullptr;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    (*snapshot)->GetKeys(
        std::move(start), std::move(next_token),
        [&result, &next_token, &num_queries](
            Status status, fidl::Array<fidl::Array<uint8_t>> keys,
            fidl::Array<uint8_t> new_next_token) {
          EXPECT_TRUE(status == Status::OK || status == Status::PARTIAL_RESULT);
          if (num_queries) {
            (*num_queries)++;
          }
          for (auto& key : keys) {
            result.push_back(std::move(key));
          }
          next_token = std::move(new_next_token);
        });
    EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
        ftl::TimeDelta::FromSeconds(1)));
  } while (next_token);
  return result;
}

fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start) {
  return SnapshotGetEntries(snapshot, std::move(start), nullptr);
}

fidl::Array<EntryPtr> SnapshotGetEntries(PageSnapshotPtr* snapshot,
                                         fidl::Array<uint8_t> start,
                                         int* num_queries) {
  fidl::Array<EntryPtr> result;
  fidl::Array<uint8_t> next_token = nullptr;
  if (num_queries) {
    *num_queries = 0;
  }
  do {
    (*snapshot)->GetEntries(
        std::move(start), std::move(next_token),
        [&result, &next_token, &num_queries](
            Status status, fidl::Array<EntryPtr> entries,
            fidl::Array<uint8_t> new_next_token) {
          EXPECT_TRUE(status == Status::OK || status == Status::PARTIAL_RESULT);
          if (num_queries) {
            (*num_queries)++;
          }
          for (auto& entry : entries) {
            result.push_back(std::move(entry));
          }
          next_token = std::move(new_next_token);
        });
    EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
        ftl::TimeDelta::FromSeconds(1)));
  } while (next_token);
  return result;
}

std::string ToString(const mx::vmo& vmo) {
  std::string value;
  bool status = mtl::StringFromVmo(vmo, &value);
  FTL_DCHECK(status);
  return value;
}

fidl::Array<uint8_t> ToArray(const mx::vmo& vmo) {
  return convert::ToArray(ToString(vmo));
}

std::string SnapshotFetchPartial(PageSnapshotPtr* snapshot,
                                 fidl::Array<uint8_t> key,
                                 int64_t offset,
                                 int64_t max_size) {
  std::string result;
  (*snapshot)->FetchPartial(std::move(key), offset, max_size,
                            [&result](Status status, mx::vmo buffer) {
                              EXPECT_EQ(status, Status::OK);
                              EXPECT_TRUE(mtl::StringFromVmo(buffer, &result));
                            });
  EXPECT_TRUE(snapshot->WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  return result;
}

void LedgerApplicationBaseTest::SetUp() {
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

void LedgerApplicationBaseTest::TearDown() {
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

mx::socket LedgerApplicationBaseTest::StreamDataToSocket(std::string data) {
  glue::SocketPair sockets;
  socket_task_runner_->PostTask(ftl::MakeCopyable([
    socket = std::move(sockets.socket1), data = std::move(data)
  ]() mutable {
    auto writer = new glue::StringSocketWriter();
    writer->Start(std::move(data), std::move(socket));
  }));
  return std::move(sockets.socket2);
}

LedgerPtr LedgerApplicationBaseTest::GetTestLedger() {
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

PagePtr LedgerApplicationBaseTest::GetTestPage() {
  fidl::InterfaceHandle<Page> page;
  Status status;

  ledger_->GetPage(nullptr, page.NewRequest(),
                   [&status](Status s) { status = s; });
  EXPECT_TRUE(ledger_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(1)));
  EXPECT_EQ(Status::OK, status);

  return fidl::InterfacePtr<Page>::Create(std::move(page));
}

PagePtr LedgerApplicationBaseTest::GetPage(const fidl::Array<uint8_t>& page_id,
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

void LedgerApplicationBaseTest::DeletePage(const fidl::Array<uint8_t>& page_id,
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

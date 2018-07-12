// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/read_data_source.h"

#include <lib/callback/capture.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace storage {
namespace {

// Data source which returns the given content byte-by-byte in separate chunks.
class SplittingDataSource : public DataSource {
 public:
  SplittingDataSource(async_dispatcher_t* dispatcher, std::string content)
      : content_(std::move(content)), index_(0), task_runner_(dispatcher) {}

  uint64_t GetSize() override { return content_.size(); };

  void Get(fit::function<void(std::unique_ptr<DataChunk>, Status)> callback)
      override {
    if (index_ >= content_.size()) {
      callback(nullptr, Status::DONE);
      return;
    }
    callback(DataChunk::Create(content_.substr(index_, 1)),
             Status::TO_BE_CONTINUED);
    ++index_;
    task_runner_.PostTask([this, callback = std::move(callback)]() mutable {
      Get(std::move(callback));
    });
  };

 private:
  const std::string content_;
  size_t index_;

  callback::ScopedTaskRunner task_runner_;
};

using ReadDataSourceTest = gtest::TestLoopFixture;

TEST_F(ReadDataSourceTest, ReadDataSource) {
  std::string expected_content = "Hello World";
  callback::ManagedContainer container;

  bool called;
  Status status;
  std::unique_ptr<DataSource::DataChunk> content;
  ReadDataSource(
      &container,
      std::make_unique<SplittingDataSource>(dispatcher(), expected_content),
      callback::Capture(callback::SetWhenCalled(&called), &status, &content));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(expected_content, content->Get());
}

TEST_F(ReadDataSourceTest, DeleteContainerWhileReading) {
  std::string expected_content = "Hello World";

  bool called;
  Status status;
  std::unique_ptr<DataSource::DataChunk> content;
  {
    callback::ManagedContainer container;
    ReadDataSource(
        &container,
        std::make_unique<SplittingDataSource>(dispatcher(), expected_content),
        callback::Capture(callback::SetWhenCalled(&called), &status, &content));
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
}

}  // namespace
}  // namespace storage

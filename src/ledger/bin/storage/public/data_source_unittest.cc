// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/data_source.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/platform/ledger_memory_estimator.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"
#include "src/ledger/lib/socket/socket_pair.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/ledger/lib/vmo/strings.h"

namespace storage {
namespace {

using ::testing::Lt;

class DataSourceTest : public ledger::TestLoopFixture {
 protected:
  ::testing::AssertionResult TestDataSource(std::string expected,
                                            std::unique_ptr<DataSource> source) {
    std::string result;
    DataSource::Status status;

    source->Get([&result, &status](std::unique_ptr<DataSource::DataChunk> data,
                                   DataSource::Status received_status) {
      status = received_status;
      if (received_status == DataSource::Status::ERROR) {
        return;
      }
      result += data->Get();
    });

    RunLoopUntilIdle();

    if (status != DataSource::Status::DONE) {
      return ::testing::AssertionFailure()
             << "Expected: " << DataSource::Status::DONE << ", but got: " << status;
    }

    if (expected != result) {
      return ::testing::AssertionFailure() << "Expected: " << expected << ", but got: " << result;
    }

    return ::testing::AssertionSuccess();
  }
};

TEST_F(DataSourceTest, String) {
  std::string value = "Hello World";

  EXPECT_TRUE(TestDataSource(value, DataSource::Create(value)));
}

TEST_F(DataSourceTest, Array) {
  std::string value = "Hello World";

  std::vector<uint8_t> array(value.size());
  memcpy(&array.at(0), value.data(), value.size());

  EXPECT_TRUE(TestDataSource(value, DataSource::Create(std::move(array))));
}

TEST_F(DataSourceTest, Vmo) {
  std::string value = "Hello World";

  ledger::SizedVmo vmo;
  EXPECT_TRUE(ledger::VmoFromString(value, &vmo));

  EXPECT_TRUE(TestDataSource(value, DataSource::Create(std::move(vmo))));
}

TEST_F(DataSourceTest, VmoIsDestroyed) {
  std::unique_ptr<ledger::Platform> platform = ledger::MakePlatform();
  uint64_t memory_before;
  ASSERT_TRUE(platform->memory_estimator()->GetCurrentProcessMemoryUsage(&memory_before));

  // Create 10 VMOs and let them get destructed.
  for (int i = 0; i < 10; ++i) {
    std::string big_value(1'000'000, 'a');
    ledger::SizedVmo vmo;
    EXPECT_TRUE(ledger::VmoFromString(big_value, &vmo));
    EXPECT_TRUE(TestDataSource(big_value, DataSource::Create(std::move(vmo))));
  }

  // Make sure there are no leftover VMOs in-memory.
  uint64_t memory_after;
  ASSERT_TRUE(platform->memory_estimator()->GetCurrentProcessMemoryUsage(&memory_after));

#if !__has_feature(address_sanitizer)
  // If the VMOs have been destroyed there should be no additional memory used at the end of this
  // test.
  EXPECT_EQ(memory_after - memory_before, 0u);
#else
  // ASAN increases memory usage. Observed values on the bots when running with ASAN are always
  // below |45'641'728|.
  EXPECT_THAT(memory_after - memory_before, Lt(46'000'000u));
#endif
}

TEST_F(DataSourceTest, Socket) {
  std::string value = "Hello World";

  EXPECT_TRUE(
      TestDataSource(value, DataSource::Create(ledger::WriteStringToSocket(value), value.size())));
}

TEST_F(DataSourceTest, SocketWrongSize) {
  std::string value = "Hello World";

  EXPECT_FALSE(TestDataSource(
      value, DataSource::Create(ledger::WriteStringToSocket(value), value.size() - 1)));
  EXPECT_FALSE(TestDataSource(
      value, DataSource::Create(ledger::WriteStringToSocket(value), value.size() + 1)));
}

TEST_F(DataSourceTest, SocketMultipleChunk) {
  const size_t nb_iterations = 2;
  std::string value = "Hello World";
  std::vector<std::string> chunks;
  DataSource::Status status;

  socket::SocketPair socket_pair;
  auto data_source =
      DataSource::Create(std::move(socket_pair.socket2), nb_iterations * value.size());

  data_source->Get([&chunks, &status](std::unique_ptr<DataSource::DataChunk> chunk,
                                      DataSource::Status new_status) {
    EXPECT_NE(DataSource::Status::ERROR, new_status);
    if (new_status == DataSource::Status::TO_BE_CONTINUED) {
      chunks.push_back(convert::ToString(chunk->Get()));
    }
    status = new_status;
  });

  for (size_t i = 0; i < nb_iterations; ++i) {
    EXPECT_EQ(chunks.size(), i);

    size_t actual = 0;
    EXPECT_EQ(socket_pair.socket1.write(0, value.c_str(), value.size(), &actual), ZX_OK);
    EXPECT_EQ(actual, value.size());

    RunLoopUntilIdle();
  }

  socket_pair.socket1.reset();
  RunLoopUntilIdle();
  EXPECT_EQ(status, DataSource::Status::DONE);

  EXPECT_EQ(chunks.size(), nb_iterations);
  for (const auto& string : chunks) {
    EXPECT_EQ(string, value);
  }
}

}  // namespace
}  // namespace storage

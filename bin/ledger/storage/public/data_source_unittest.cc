// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/public/data_source.h"

#include "apps/ledger/src/glue/socket/socket_pair.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"

namespace storage {
namespace {

class DataSourceTest : public test::TestWithMessageLoop {
 protected:
  ::testing::AssertionResult TestDataSource(
      std::string expected,
      std::unique_ptr<DataSource> source) {
    std::string result;
    DataSource::Status status;

    source->Get(
        [this, &result, &status](std::unique_ptr<DataSource::DataChunk> data,
                                 DataSource::Status received_status) {
          status = received_status;
          if (received_status == DataSource::Status::ERROR) {
            message_loop_.PostQuitTask();
            return;
          }

          result += data->Get().ToString();

          if (received_status == DataSource::Status::DONE) {
            message_loop_.PostQuitTask();
          }
        });

    if (RunLoopWithTimeout()) {
      return ::testing::AssertionFailure() << "Timed out.";
    }

    if (status != DataSource::Status::DONE) {
      return ::testing::AssertionFailure()
             << "Expected: " << DataSource::Status::DONE
             << ", but got: " << status;
    }

    if (expected != result) {
      return ::testing::AssertionFailure()
             << "Expected: " << expected << ", but got: " << result;
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

  fidl::Array<uint8_t> array;
  array.resize(value.size());
  memcpy(&array[0], value.data(), value.size());

  EXPECT_TRUE(TestDataSource(value, DataSource::Create(std::move(array))));
}

TEST_F(DataSourceTest, Vmo) {
  std::string value = "Hello World";

  zx::vmo vmo;
  EXPECT_TRUE(fsl::VmoFromString(value, &vmo));

  EXPECT_TRUE(TestDataSource(value, DataSource::Create(std::move(vmo))));
}

TEST_F(DataSourceTest, Socket) {
  std::string value = "Hello World";

  EXPECT_TRUE(TestDataSource(
      value,
      DataSource::Create(fsl::WriteStringToSocket(value), value.size())));
}

TEST_F(DataSourceTest, SocketWrongSize) {
  std::string value = "Hello World";

  EXPECT_FALSE(TestDataSource(
      value,
      DataSource::Create(fsl::WriteStringToSocket(value), value.size() - 1)));
  EXPECT_FALSE(TestDataSource(
      value,
      DataSource::Create(fsl::WriteStringToSocket(value), value.size() + 1)));
}

TEST_F(DataSourceTest, SocketMultipleChunk) {
  const size_t nb_iterations = 2;
  std::string value = "Hello World";
  std::vector<std::string> chunks;
  DataSource::Status status;

  glue::SocketPair socket_pair;
  auto data_source = DataSource::Create(std::move(socket_pair.socket2),
                                        nb_iterations * value.size());

  data_source->Get(
      [this, &chunks, &status](std::unique_ptr<DataSource::DataChunk> chunk,
                               DataSource::Status new_status) {
        EXPECT_NE(DataSource::Status::ERROR, new_status);
        if (new_status == DataSource::Status::TO_BE_CONTINUED) {
          chunks.push_back(chunk->Get().ToString());
        }
        status = new_status;
        message_loop_.PostQuitTask();
      });

  for (size_t i = 0; i < nb_iterations; ++i) {
    EXPECT_EQ(i, chunks.size());

    size_t actual = 0;
    EXPECT_EQ(ZX_OK, socket_pair.socket1.write(0, value.c_str(), value.size(),
                                               &actual));
    EXPECT_EQ(value.size(), actual);

    EXPECT_FALSE(RunLoopWithTimeout());
  }

  socket_pair.socket1.reset();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(DataSource::Status::DONE, status);

  EXPECT_EQ(nb_iterations, chunks.size());
  for (const auto& string : chunks) {
    EXPECT_EQ(value, string);
  }
}

}  // namespace
}  // namespace storage

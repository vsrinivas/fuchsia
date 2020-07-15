// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/security/codelab/smart_door_memory/src/smart_door_memory_server_app.h"

namespace smart_door_memory {
namespace test {

using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::Memory_GenerateToken_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetReader_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetWriter_Result;
using fuchsia::security::codelabsmartdoor::MemoryPtr;
using fuchsia::security::codelabsmartdoor::Reader_Read_Result;
using fuchsia::security::codelabsmartdoor::ReaderPtr;
using fuchsia::security::codelabsmartdoor::Token;
using fuchsia::security::codelabsmartdoor::Writer;
using fuchsia::security::codelabsmartdoor::Writer_Write_Result;
using fuchsia::security::codelabsmartdoor::WriterPtr;

class SmartDoorMemoryServerAppForTest : public SmartDoorMemoryServerApp {
 public:
  SmartDoorMemoryServerAppForTest(std::unique_ptr<sys::ComponentContext> context)
      : SmartDoorMemoryServerApp(std::move(context)) {}
};

class SmartDoorMemoryServerTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    server_.reset(new SmartDoorMemoryServerAppForTest(provider_.TakeContext()));
  }

  void TearDown() override {
    server_.reset();
    TestLoopFixture::TearDown();
  }

  MemoryPtr getSmartDoorMemory() {
    MemoryPtr smart_door_memory;
    provider_.ConnectToPublicService(smart_door_memory.NewRequest());
    return smart_door_memory;
  }

 private:
  std::unique_ptr<SmartDoorMemoryServerAppForTest> server_;
  sys::testing::ComponentContextProvider provider_;
};

TEST_F(SmartDoorMemoryServerTest, TestGenerateToken) {
  MemoryPtr smart_door_memory = getSmartDoorMemory();
  Memory_GenerateToken_Result result;
  smart_door_memory->GenerateToken([&](Memory_GenerateToken_Result s) { result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_response());
}

TEST_F(SmartDoorMemoryServerTest, TestWriterReader) {
  MemoryPtr smart_door_memory = getSmartDoorMemory();
  Memory_GenerateToken_Result token_result;
  smart_door_memory->GenerateToken(
      [&](Memory_GenerateToken_Result s) { token_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(token_result.is_response());
  auto token = std::move(token_result.response().token);

  // Try to read from a file that has not been written to.
  Memory_GetReader_Result get_reader_result;
  ReaderPtr reader;
  Token read_token;
  read_token.set_id(token.id());
  smart_door_memory->GetReader(
      std::move(read_token), reader.NewRequest(),
      [&](Memory_GetReader_Result s) { get_reader_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(get_reader_result.is_err());

  Memory_GetWriter_Result get_writer_result;
  WriterPtr writer;
  Token write_token;
  write_token.set_id(token.id());
  smart_door_memory->GetWriter(
      std::move(write_token), writer.NewRequest(),
      [&](Memory_GetWriter_Result s) { get_writer_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_FALSE(get_writer_result.is_err());

  // Write something into the file.
  Writer_Write_Result write_result;
  std::vector<uint8_t> data(16, 1u);
  writer->Write(data, [&](Writer_Write_Result s) { write_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(16u, write_result.response().bytes_written);

  // Read the content out.
  read_token.set_id(token.id());
  smart_door_memory->GetReader(
      std::move(read_token), reader.NewRequest(),
      [&](Memory_GetReader_Result s) { get_reader_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_FALSE(get_reader_result.is_err());

  Reader_Read_Result read_result;
  reader->Read([&](Reader_Read_Result s) { read_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(read_result.is_response());
  EXPECT_EQ(data, read_result.response().bytes);

  // Write something else.
  data.clear();
  data.push_back(2u);
  writer->Write(data, [&](Writer_Write_Result s) { write_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(1u, write_result.response().bytes_written);

  // Read should read the new content out.
  reader->Read([&](Reader_Read_Result s) { read_result = std::move(s); });
  RunLoopUntilIdle();
  EXPECT_TRUE(read_result.is_response());
  EXPECT_EQ(data, read_result.response().bytes);
}

}  // namespace test
}  // namespace smart_door_memory

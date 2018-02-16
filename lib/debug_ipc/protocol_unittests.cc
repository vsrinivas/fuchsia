// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol_helpers.h"
#include "gtest/gtest.h"

namespace debug_ipc {

namespace {

template<typename RequestType>
bool SerializeDeserializeRequest(const RequestType& in, RequestType* out) {
  MessageWriter writer;
  uint32_t in_transaction_id = 32;
  WriteRequest(in, in_transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();

  MessageReader reader(std::move(serialized));
  uint32_t out_transaction_id = 0;
  if (!ReadRequest(&reader, out, &out_transaction_id))
    return false;
  EXPECT_EQ(in_transaction_id, out_transaction_id);
  return true;
}

template<typename ReplyType>
bool SerializeDeserializeReply(const ReplyType& in, ReplyType* out) {
  MessageWriter writer;
  uint32_t in_transaction_id = 32;
  WriteReply(in, in_transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();

  MessageReader reader(std::move(serialized));
  uint32_t out_transaction_id = 0;
  if (!ReadReply(&reader, out, &out_transaction_id))
    return false;
  EXPECT_EQ(in_transaction_id, out_transaction_id);
  return true;
}

}  // namespace

TEST(Protocol, HelloRequest) {
  HelloRequest initial;
  HelloRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
}

TEST(Protocol, HelloReply) {
  HelloReply initial;
  initial.version = 12345678;
  HelloReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.version, second.version);
}

TEST(Protocol, LaunchRequest) {
  LaunchRequest initial;
  initial.argv.push_back("/usr/bin/WINWORD.EXE");
  initial.argv.push_back("--dosmode");

  LaunchRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  ASSERT_EQ(initial.argv.size(), second.argv.size());
  for (size_t i = 0; i < initial.argv.size(); i++)
    EXPECT_EQ(initial.argv[i], second.argv[i]);
}

TEST(Protocol, LaunchReply) {
  LaunchReply initial;
  initial.status = 67;
  initial.process_koid = 0x1234;

  LaunchReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.status, second.status);
  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, ProcessTreeRequest) {
  ProcessTreeRequest initial;
  ProcessTreeRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
}

TEST(Protocol, ProcessTreeReply) {
  ProcessTreeReply initial;
  initial.root.type = ProcessTreeRecord::Type::kJob;
  initial.root.koid = 1234;
  initial.root.name = "root";

  initial.root.children.resize(1);
  initial.root.children[0].type = ProcessTreeRecord::Type::kProcess;
  initial.root.children[0].koid = 3456;
  initial.root.children[0].name = "hello";

  ProcessTreeReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  EXPECT_EQ(initial.root.type, second.root.type);
  EXPECT_EQ(initial.root.koid, second.root.koid);
  EXPECT_EQ(initial.root.name, second.root.name);
  ASSERT_EQ(initial.root.children.size(), second.root.children.size());
  EXPECT_EQ(initial.root.children[0].type, second.root.children[0].type);
  EXPECT_EQ(initial.root.children[0].koid, second.root.children[0].koid);
  EXPECT_EQ(initial.root.children[0].name, second.root.children[0].name);
}

TEST(Protocol, ThreadsRequest) {
  ThreadsRequest initial;
  initial.process_koid = 36473476;

  ThreadsRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, ThreadsReply) {
  ThreadsReply initial;
  initial.threads.resize(2);
  initial.threads[0].koid = 1234;
  initial.threads[0].name = "one";
  initial.threads[1].koid = 7634;
  initial.threads[1].name = "two";

  ThreadsReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(initial.threads.size(), second.threads.size());
  EXPECT_EQ(initial.threads[0].koid, second.threads[0].koid);
  EXPECT_EQ(initial.threads[0].name, second.threads[0].name);
  EXPECT_EQ(initial.threads[1].koid, second.threads[1].koid);
  EXPECT_EQ(initial.threads[1].name, second.threads[1].name);
}

TEST(Protocol, ReadMemoryRequest) {
  ReadMemoryRequest initial;
  initial.process_koid = 91823765;
  initial.address = 983462384;
  initial.size = 93453926;

  ReadMemoryRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.address, second.address);
  EXPECT_EQ(initial.size, second.size);
}

TEST(Protocol, ReadMemoryReply) {
  ReadMemoryReply initial;
  initial.blocks.resize(2);
  initial.blocks[0].address = 876234;
  initial.blocks[0].valid = true;
  initial.blocks[0].size = 12;
  for (uint64_t i = 0; i < initial.blocks[0].size; i++)
    initial.blocks[0].data.push_back(static_cast<uint8_t>(i));

  initial.blocks[1].address = 89362454;
  initial.blocks[1].valid = false;
  initial.blocks[1].size = 0;

  ReadMemoryReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(initial.blocks.size(), second.blocks.size());

  EXPECT_EQ(initial.blocks[0].address, second.blocks[0].address);
  EXPECT_EQ(initial.blocks[0].valid, second.blocks[0].valid);
  EXPECT_EQ(initial.blocks[0].size, second.blocks[0].size);
  EXPECT_EQ(second.blocks[0].size, second.blocks[0].data.size());
  for (uint64_t i = 0; i < second.blocks[0].size; i++)
    EXPECT_EQ(static_cast<uint8_t>(i), second.blocks[0].data[i]);

  EXPECT_EQ(initial.blocks[1].address, second.blocks[1].address);
  EXPECT_EQ(initial.blocks[1].valid, second.blocks[1].valid);
  EXPECT_EQ(initial.blocks[1].size, second.blocks[1].size);
  EXPECT_TRUE(second.blocks[1].data.empty());
}

}  // namespace debug_ipc

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(brettw) figure out testing.
#if 0

#include "client/client_protocol.h"
#include "shared/message_reader.h"
#include "shared/message_writer.h"
#include "service/service_protocol.h"

#include "testing/gtest/include/gtest/gtest.h"

TEST(Protocol, HelloRequest) {
    HelloRequest initial;
    MessageWriter writer;
    WriteRequest(initial, &writer);

    uint64_t serialized_size = 0;
    const char* serialized = writer.GetDataAndWriteSize(&serialized_size);
    EXPECT_EQ(MsgHeader::kSerializedHeaderSize, serialized_size);

    MessageReader reader(serialized, serialized_size);
    HelloRequest second;
    ASSERT_TRUE(ReadRequest(&reader, &second));
}

TEST(Protocol, HelloReply) {
    HelloReply initial;
    initial.version = 12345678;
    MessageWriter writer;
    WriteReply(initial, &writer);

    uint64_t serialized_size = 0;
    const char* serialized = writer.GetDataAndWriteSize(&serialized_size);
    EXPECT_EQ(serialized_size, sizeof(HelloReply) + MsgHeader::kSerializedHeaderSize);

    MessageReader reader(serialized, serialized_size);
    HelloReply second;
    ASSERT_TRUE(ReadReply(&reader, &second));
    EXPECT_EQ(initial.version, second.version);
}

TEST(Protocol, ProcessTreeRequest) {
    ProcessTreeRequest initial;
    MessageWriter writer;
    WriteRequest(initial, &writer);

    uint64_t serialized_size = 0;
    const char* serialized = writer.GetDataAndWriteSize(&serialized_size);
    EXPECT_EQ(serialized_size, sizeof(ProcessTreeRequest) + MsgHeader::kSerializedHeaderSize);

    MessageReader reader(serialized, serialized_size);
    ProcessTreeRequest second;
    ASSERT_TRUE(ReadRequest(&reader, &second));
}

/*
TEST(Protocol, ProcessTreeReply) {
    ProcessTreeReply initial;
    initial.root.type = ProcessTreeRecord::Type::kJob;
    initial.root.

    MessageWriter writer;
    WriteProcessTreeReply(initial, &writer);

    uint64_t serialized_size = 0;
    const char* serialized = writer.GetDataAndWriteSize(&serialized_size);

    MessageReader reader(serialized, serialized_size);
    ProcessTreeReply second;
    ASSERT_TRUE(ReadProcessTreeReply(&reader, &second));
    EXPECT_EQ(sizeof(ProcessTreeReply), second.msg_size);
    EXPECT_EQ(initial.type, second.type);
}*/

#endif

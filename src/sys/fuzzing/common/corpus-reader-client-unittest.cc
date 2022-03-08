// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/corpus-reader-client.h"

#include <vector>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/corpus-reader.h"

namespace fuzzing {

// Test fixtures.

class CorpusReaderClientTest : public AsyncTest {
 protected:
  auto Connect(CorpusReaderClient& client) {
    auto server = std::make_unique<FakeCorpusReader>(executor());
    server->Bind(client.NewRequest());
    return server;
  }
};

// Unit tests.

TEST_F(CorpusReaderClientTest, SendEmpty) {
  CorpusReaderClient client(executor());
  auto server = Connect(client);
  FUZZING_EXPECT_OK(client.Send(std::vector<Input>()));
  RunUntilIdle();
  const auto& corpus = server->corpus();
  ASSERT_EQ(corpus.size(), 1U);
  EXPECT_EQ(corpus[0], Input());
}

TEST_F(CorpusReaderClientTest, Send) {
  CorpusReaderClient client(executor());
  auto server = Connect(client);
  Input inputs[] = {
      {},                              // input0 should be skipped and sent last.
      {0xde, 0xad}, {0xbe, 0xef}, {},  // input3 is empty and should be skipped.
      {0xfe, 0xed}, {0xfa, 0xce},
  };
  std::vector<Input> copy;
  for (const auto& input : inputs) {
    copy.emplace_back(input.Duplicate());
  }
  FUZZING_EXPECT_OK(client.Send(std::move(copy)));
  RunUntilIdle();
  const auto& corpus = server->corpus();
  ASSERT_EQ(corpus.size(), 5U);
  EXPECT_EQ(corpus[0], inputs[1]);
  EXPECT_EQ(corpus[1], inputs[2]);
  EXPECT_EQ(corpus[2], inputs[4]);
  EXPECT_EQ(corpus[3], inputs[5]);
  EXPECT_EQ(corpus[4], inputs[0]);
}

TEST_F(CorpusReaderClientTest, SendPartial) {
  CorpusReaderClient client(executor());
  auto server = Connect(client);
  Input inputs[] = {
      {0xde, 0xad},
      {0xbe, 0xef},
      {0xfe, 0xed},
      {0xfa, 0xce},
  };
  std::vector<Input> copy;
  for (const auto& input : inputs) {
    copy.emplace_back(input.Duplicate());
  }
  server->set_error_after(2);
  FUZZING_EXPECT_ERROR(client.Send(std::move(copy)), ZX_ERR_INTERNAL);
  RunUntilIdle();
  const auto& corpus = server->corpus();
  ASSERT_EQ(corpus.size(), 2U);
  EXPECT_EQ(corpus[0], inputs[0]);
  EXPECT_EQ(corpus[1], inputs[1]);
}

}  // namespace fuzzing

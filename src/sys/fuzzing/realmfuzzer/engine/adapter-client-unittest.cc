// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/adapter-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/realmfuzzer/testing/adapter.h"

namespace fuzzing {

// Test fixtures

class TargetAdapterClientTest : public AsyncTest {
 protected:
  std::unique_ptr<FakeTargetAdapter> Bind(TargetAdapterClient* client) {
    auto adapter = std::make_unique<FakeTargetAdapter>(executor());
    client->set_handler(adapter->GetHandler());
    return adapter;
  }
};

// Unit tests

TEST_F(TargetAdapterClientTest, GetParameters) {
  TargetAdapterClient client(executor());
  client.Configure(MakeOptions());
  auto adapter = Bind(&client);

  std::vector<std::string> params{"-s", "--long", "positional", "--", "ignored"};
  adapter->SetParameters(params);
  FUZZING_EXPECT_OK(client.GetParameters(), params);
  RunUntilIdle();
}

TEST_F(TargetAdapterClientTest, GetSeedCorpusDirectories) {
  TargetAdapterClient client(executor());

  std::vector<std::string> params = {"-flags", "--but", "-no=positional-args"};
  auto actual = client.GetSeedCorpusDirectories(params);
  std::vector<std::string> expected;
  EXPECT_EQ(actual, expected);

  params = {"-a", "single", "--positional-arg"};
  actual = client.GetSeedCorpusDirectories(params);
  expected = {"single"};
  EXPECT_EQ(actual, expected);

  params = {"multiple", "positional", "args"};
  actual = client.GetSeedCorpusDirectories(params);
  expected = {"multiple", "positional", "args"};
  EXPECT_EQ(actual, expected);

  params = {"--includes", "empty", "", "string"};
  actual = client.GetSeedCorpusDirectories(params);
  expected = {"empty", "string"};
  EXPECT_EQ(actual, expected);

  params = {"--includes", "ignored", "--", "string"};
  actual = client.GetSeedCorpusDirectories(params);
  expected = {"ignored"};
  EXPECT_EQ(actual, expected);
}

TEST_F(TargetAdapterClientTest, TestOneInput) {
  TargetAdapterClient client(executor());
  client.Configure(MakeOptions());
  auto adapter = Bind(&client);
  Input sent("foo");
  FUZZING_EXPECT_OK(adapter->TestOneInput(), sent.Duplicate());
  FUZZING_EXPECT_OK(client.TestOneInput(sent));
  RunUntilIdle();
}

TEST_F(TargetAdapterClientTest, Disconnect) {
  TargetAdapterClient client(executor());
  client.Configure(MakeOptions());
  auto adapter = Bind(&client);

  // Make sure the client is connected.
  Input sent1("foo");
  FUZZING_EXPECT_OK(client.TestOneInput(sent1));
  FUZZING_EXPECT_OK(adapter->TestOneInput(), std::move(sent1));
  RunUntilIdle();

  // Disconnect it.
  FUZZING_EXPECT_OK(adapter->AwaitDisconnect());
  client.Disconnect();
  RunUntilIdle();

  // Check that it reconnects automatically.
  Input sent2("bar");
  FUZZING_EXPECT_OK(client.TestOneInput(sent2));
  FUZZING_EXPECT_OK(adapter->TestOneInput(), std::move(sent2));
  RunUntilIdle();
}

}  // namespace fuzzing
